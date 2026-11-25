#include "cottage/mpc_planner.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace enevina::cottage {
namespace {
bool choose_setpoints(const PumpHeatingLimits& limits,const ComfortBand& band,
                      double thermostat_band,MpcRunControls& controls) {
    if(!limits.heating || !std::isfinite(limits.step_c) || limits.step_c<=0 ||
       !std::isfinite(limits.minimum_c) || !std::isfinite(limits.maximum_c) ||
       limits.minimum_c<5 || limits.maximum_c>35 || limits.minimum_c>limits.maximum_c)
        return false;
    const double lo=std::ceil(limits.minimum_c/limits.step_c)*limits.step_c;
    const double hi=std::floor(limits.maximum_c/limits.step_c)*limits.step_c;
    if(lo>hi)return false;
    const double candidates[]={band.lower_c,band.target_c,
        band.target_c+thermostat_band,band.upper_c+thermostat_band};
    controls.heat_setpoint_count=0;
    controls.permit_off=limits.off;
    for(const auto raw:candidates) {
        const double setpoint=std::clamp(std::round(raw/limits.step_c)*limits.step_c,lo,hi);
        if(!limits.accepts(setpoint))return false;
        if(controls.heat_setpoint_count==0 ||
           setpoint>controls.heat_setpoints_c[controls.heat_setpoint_count-1])
            controls.heat_setpoints_c[controls.heat_setpoint_count++]=setpoint;
    }
    return true;
}
}
PlannerOutput plan_mpc(const PlannerInput& in,PlanRow* rows) {
    PlannerOutput out{};
    constexpr double nan=std::numeric_limits<double>::quiet_NaN();
    out.energy_kwh=out.cost=nan;
    MpcPlanRecord& rec=out.record;
    rec.computed_utc=in.now_utc;
    rec.first_utc=(in.now_utc/900)*900;
    rec.mode_generation=in.mode_generation;
    rec.defrost_reports=in.defrost_reports;
    rec.config_generation=in.config_generation;
    rec.settings_generation=in.settings_generation;
    rec.target_c=in.target_c;
    rec.limits=in.limits;
    rec.profile=in.profile;
    rec.initial_heat=in.pump_heat;
    rec.low_temperature_cycle=in.low_temperature_cycle && low_temperature_room_target(in.target_c);
    const auto no_plan=[&](MpcFallback why) {rec.has_plan=false;rec.planning_fallback=why;return out;};
    if(in.pump_defrost_hold && in.pump_heat)return no_plan(MpcFallback::DefrostHold);
    if(!in.identifier) return no_plan(MpcFallback::ModelLearning);
    rec.variable_setpoints=in.identifier->variable_setpoints();
    if(rec.variable_setpoints)rec.initial_setpoint_c=in.pump_setpoint_c;
    const CalibrationReport cal=in.identifier->report(in.now_utc);
    rec.model_revision=cal.revision;
    if(cal.state!=CalibrationState::Calibrated)
        return no_plan(cal.state==CalibrationState::Degraded?MpcFallback::ModelDegraded:
            cal.state==CalibrationState::Validating?MpcFallback::ModelValidating:
            MpcFallback::ModelLearning);
    if(!in.heating_allowed) return no_plan(MpcFallback::NoHeatingMode);
    ComfortBand band{};
    {   ComfortBand model_band{};
        // A target outside the HEAT 16 C model is reported as such; a target
        // inside it with malformed / contradictory owner limits is reported
        // as invalid limits. The MPC never invents other limits.
        if(!in.target_valid || !mpc_comfort_band(in.target_c,model_band,{},rec.variable_setpoints))
            return no_plan(MpcFallback::TargetOutsideModel);
        if(!mpc_comfort_band(in.target_c,band,in.limits,rec.variable_setpoints))
            return no_plan(MpcFallback::ComfortLimitsInvalid);
    }
    if(rec.variable_setpoints)apply_mpc_profile(band,in.profile);
    rec.band=band;
    if(!in.temperature_fresh || !std::isfinite(in.indoor_c))
        return no_plan(MpcFallback::TemperatureStale);
    if(!in.pump_state_known) return no_plan(MpcFallback::PumpStateUnknown);
    if(in.indoor_c>band.upper_c) return no_plan(MpcFallback::AboveComfortCeiling);
    if(!in.horizon || in.horizon->bridge.status!=BridgeStatus::Ready)
        return no_plan(in.horizon && (in.horizon->bridge.status==BridgeStatus::Gap ||
            in.horizon->bridge.status==BridgeStatus::BadWeather) &&
            (in.horizon->price_slots>0 || in.horizon->weather_slots>0)?
            MpcFallback::HorizonTooShort:MpcFallback::ForecastUnavailable);
    BridgeResult& b=in.horizon->bridge;
    if(b.count<in.min_slots || b.count>max_horizon_slots)
        return no_plan(MpcFallback::HorizonTooShort);
    if(b.first_utc!=rec.first_utc) return no_plan(MpcFallback::PlanStale);
    Parameters p{};
    bool kwh=false;
    switch(in.identifier->build_parameters(in.now_utc,band,in.meter_pump_only,
                                           in.currency_scale,p,kwh,in.meter_incremental)) {
    case ModelBuildError::None:break;
    case ModelBuildError::OutsideDomain:return no_plan(MpcFallback::TargetOutsideModel);
    case ModelBuildError::NotCalibrated:return no_plan(MpcFallback::ModelValidating);
    case ModelBuildError::Implausible:
    case ModelBuildError::NumericalFailure:return no_plan(MpcFallback::SolverFailed);
    }
    if(rec.low_temperature_cycle) {
        // The full-slot solver can express 15-min pulses, but the live
        // independent thermostat may stop one earlier at the room ceiling.
        // A rejected full-slot plan falls back; it is never forced through.
        p.min_heat_minutes=cn105_low_temperature_dwell_minutes;
        p.min_off_minutes=cn105_low_temperature_dwell_minutes;
    }
    out.params=p;
    out.energy_in_kwh=kwh;
    out.energy_incremental=kwh && cal.model.electric_incremental;
    // The model may not extrapolate far beyond the outdoor range it learned.
    const ModelEstimate& m=cal.model;
    const double lo=m.outdoor_min_c-CalibrationLimits::outdoor_margin_c;
    const double hi=m.outdoor_max_c+CalibrationLimits::outdoor_margin_c;
    // This gain is fitted to actual room response, including steady
    // indoor equipment such as mining devices. Baseline electrical watts
    // are NOT added a second time to this normalised thermal model.
    const double gain_kw=std::clamp(m.gain_rise_k*p.loss_kw_per_c,-29.0,29.0);
    // An uncertain weather tail does not invalidate today's supported
    // prefix. Never extrapolate through the first unsupported slot; retain
    // the existing minimum horizon, replay and terminal-temperature gates.
    for(size_t i=0;i<b.count;++i) {
        if(!std::isfinite(b.periods[i].outdoor_c) ||
           b.periods[i].outdoor_c<lo || b.periods[i].outdoor_c>hi) {
            if(i<in.min_slots)return no_plan(MpcFallback::OutdoorOutsideLearned);
            b.count=i;in.horizon->limit=HorizonLimit::LearnedOutdoorRange;
            break;
        }
    }
    Period* periods=b.periods.data();
    for(size_t i=0;i<b.count;++i) periods[i].internal_gain_kw=gain_kw;
    const State initial{in.indoor_c,in.pump_heat,in.minutes_in_mode};
    MpcRunControls controls=in.controls;
    if(rec.variable_setpoints) {
        controls.initial_setpoint_c=in.pump_setpoint_c;
        if(controls.setpoint_change_penalty==0)
            controls.setpoint_change_penalty=p.switching_penalty_eur/2;
        if(!choose_setpoints(in.pump_limits,band,p.thermostat_band_c,controls))
            return no_plan(MpcFallback::PumpStateUnknown);
        if(rec.low_temperature_cycle) {
            if(!in.pump_limits.accepts(cn105_minimum_heat_setpoint_c))
                return no_plan(MpcFallback::PumpStateUnknown);
            controls.heat_setpoint_count=1;
            controls.heat_setpoints_c[0]=cn105_minimum_heat_setpoint_c;
        }
    }
    // Receding horizon: end at the room target (inside the band), never
    // "at least as warm as now" (see MpcRunControls::terminal_min_c).
    controls.terminal_min_c=std::clamp(band.target_c,p.planning_min_c+p.planning_uncertainty_c,
                                       p.maximum_indoor_c-0.1);
    out.result=optimize(p,initial,periods,b.count,in.memory_budget_bytes,controls);
    rec.status=out.result.status;
    rec.count=out.result.count;
    if(out.result.status!=MpcStatus::Ok || out.result.count==0)
        return no_plan(fallback_for_status(out.result.status));
    rec.has_plan=true;
    rec.planning_fallback=MpcFallback::None;
    rec.first_heat=out.result.heat_16_c[0]!=0;
    rec.first_setpoint_c=out.result.setpoint_c100[0]/100.0;
    State s=initial;
    double energy=0,cost=0;
    for(size_t i=0;i<out.result.count;++i) {
        const double setpoint=out.result.setpoint_c100[i]/100.0;
        const bool heat=setpoint>0;
        const Step st=advance_setpoint(p,s,periods[i],setpoint);
        s=st.next;
        energy+=st.electric_kwh;cost+=st.cost_eur;
        if(rows) {
            PlanRow& r=rows[i];
            r.utc=b.first_utc+static_cast<int64_t>(i)*900;
            r.heat=heat;r.setpoint_c=setpoint;
            r.indoor_c=s.indoor_c;r.outdoor_c=periods[i].outdoor_c;
            r.price=periods[i].total_price_eur_per_kwh;
            r.energy_kwh=kwh?st.electric_kwh:nan;
            r.cost=kwh?st.cost_eur:nan;
        }
    }
    out.rows=rows?out.result.count:0;
    if(kwh) {out.energy_kwh=energy;out.cost=cost;}
    return out;
}
} // namespace enevina::cottage
