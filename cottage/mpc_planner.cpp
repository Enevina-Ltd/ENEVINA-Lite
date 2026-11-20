#include "cottage/mpc_planner.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace enevina::cottage {
PlannerOutput plan_mpc(const PlannerInput& in,PlanRow* rows) {
    PlannerOutput out{};
    constexpr double nan=std::numeric_limits<double>::quiet_NaN();
    out.energy_kwh=out.cost=nan;
    MpcPlanRecord& rec=out.record;
    rec.computed_utc=in.now_utc;
    rec.first_utc=(in.now_utc/900)*900;
    rec.mode_generation=in.mode_generation;
    rec.config_generation=in.config_generation;
    rec.settings_generation=in.settings_generation;
    rec.target_c=in.target_c;
    rec.initial_heat=in.pump_heat;
    const auto no_plan=[&](MpcFallback why) {rec.has_plan=false;rec.planning_fallback=why;return out;};
    if(!in.identifier) return no_plan(MpcFallback::ModelLearning);
    const CalibrationReport cal=in.identifier->report(in.now_utc);
    rec.model_revision=cal.revision;
    if(cal.state!=CalibrationState::Calibrated)
        return no_plan(cal.state==CalibrationState::Degraded?MpcFallback::ModelDegraded:
            cal.state==CalibrationState::Validating?MpcFallback::ModelValidating:
            MpcFallback::ModelLearning);
    if(!in.heating_allowed) return no_plan(MpcFallback::NoHeatingMode);
    ComfortBand band{};
    if(!in.target_valid || !mpc_comfort_band(in.target_c,band))
        return no_plan(MpcFallback::TargetOutsideModel);
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
                                           in.currency_scale,p,kwh)) {
    case ModelBuildError::None:break;
    case ModelBuildError::OutsideDomain:return no_plan(MpcFallback::TargetOutsideModel);
    case ModelBuildError::NotCalibrated:return no_plan(MpcFallback::ModelValidating);
    case ModelBuildError::Implausible:
    case ModelBuildError::NumericalFailure:return no_plan(MpcFallback::SolverFailed);
    }
    out.params=p;
    out.energy_in_kwh=kwh;
    // The model may not extrapolate far beyond the outdoor range it learned.
    const ModelEstimate& m=cal.model;
    const double lo=m.outdoor_min_c-CalibrationLimits::outdoor_margin_c;
    const double hi=m.outdoor_max_c+CalibrationLimits::outdoor_margin_c;
    const double gain_kw=std::clamp(m.gain_rise_k*p.loss_kw_per_c,-29.0,29.0);
    for(size_t i=0;i<b.count;++i)
        if(b.periods[i].outdoor_c<lo || b.periods[i].outdoor_c>hi)
            return no_plan(MpcFallback::OutdoorOutsideLearned);
    Period* periods=b.periods.data();
    for(size_t i=0;i<b.count;++i) periods[i].internal_gain_kw=gain_kw;
    const State initial{in.indoor_c,in.pump_heat,in.minutes_in_mode};
    MpcRunControls controls=in.controls;
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
    State s=initial;
    double energy=0,cost=0;
    for(size_t i=0;i<out.result.count;++i) {
        const bool heat=out.result.heat_16_c[i]!=0;
        const Step st=advance(p,s,periods[i],heat);
        s=st.next;
        energy+=st.electric_kwh;cost+=st.cost_eur;
        if(rows) {
            PlanRow& r=rows[i];
            r.utc=b.first_utc+static_cast<int64_t>(i)*900;
            r.heat=heat;r.indoor_c=s.indoor_c;r.outdoor_c=periods[i].outdoor_c;
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
