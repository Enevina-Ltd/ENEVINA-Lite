// Building identification: parameter recovery, honest calibration states,
// meter-scope rule, walk-forward validation, degradation and persistence.
#include "cottage/building_identification.hpp"
#include "cottage/cn105_gateway_request.hpp"
#include "cottage/optimization_profile.hpp"
#include "tests/cottage_closed_loop.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace enevina::cottage;
using closed_loop::Rng;

namespace {
int failures=0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); ++failures; } } while(0)

constexpr int64_t t_start=1768168800-30*86400;

struct Sim {
    Parameters truth=closed_loop::true_cottage();
    double gain_kw=0.1, solar_kw=0.0, noise_c=0.03, forecast_err_c=0.3;
    double target=12.0;
    bool pump_meter=false;
    bool always_off=false;
    State st{12.0,false,120};
    int64_t utc=t_start;
    Rng rng{42};
    double meas=12.0;
    unsigned since=120;
    double outdoor(int64_t t) const {
        const double h=((t+7200)%86400)/3600.0;
        return -3.0+4.0*std::sin(6.283185307179586*(h-9)/24.0)+
            3.0*std::sin(6.283185307179586*double(t-t_start)/(86400.0*4.3));
    }
    SlotObservation step() {
        bool want=st.heat_requested;
        if(always_off) want=false;
        else if(meas<=target-0.5) want=true;
        else if(meas>=target+0.5) want=false;
        if(want!=st.heat_requested && since<30) want=st.heat_requested;
        const double to=outdoor(utc);
        const double solar=solar_kw*std::max(0.0,std::sin(6.283185307179586*
            (((utc+7200)%86400)/86400.0-0.25)));
        const Period d{to,0.1,gain_kw+solar,1.0};
        SlotObservation o{};
        o.slot_utc=utc;
        o.indoor_valid=true;
        o.indoor_start_c=meas;
        const Step s=advance(truth,st,d,want);
        since=want!=st.heat_requested?15:std::min(600u,since+15);
        st=s.next;
        meas=std::round((st.indoor_c+noise_c*rng.normal())*100.0)/100.0;
        o.indoor_end_c=meas;
        o.outdoor_valid=true;
        o.outdoor_c=to+forecast_err_c*rng.normal();
        o.heat_valid=true;o.heat_requested=want;o.in_model_domain=true;
        o.electric_valid=true;o.electric_kwh=s.electric_kwh*(1.0+0.02*rng.normal());
        o.meter_pump_only=pump_meter;
        utc+=900;
        return o;
    }
};
double rel(double a,double b){return std::fabs(a-b)/std::fabs(b);}

void test_last_observation_reason() {
    BuildingIdentifier id;
    SlotObservation o{};
    o.slot_utc=t_start;
    o.indoor_start_c=12;o.indoor_end_c=11.95;o.outdoor_c=-2;
    o.indoor_valid=o.outdoor_valid=o.heat_valid=true;
    CHECK(!id.add(o));
    CHECK(id.report(o.slot_utc).last_observation==LearningObservation::NeedPrevious);
    o.slot_utc+=900;CHECK(id.add(o));
    CHECK(id.report(o.slot_utc).last_observation==LearningObservation::Accepted);
    o.slot_utc+=900;o.heat_valid=false;CHECK(!id.add(o));
    CHECK(id.report(o.slot_utc).last_observation==LearningObservation::RequestInvalid);
    o.slot_utc+=900;o.heat_valid=true;o.indoor_valid=false;CHECK(!id.add(o));
    CHECK(id.report(o.slot_utc).last_observation==LearningObservation::RoomInvalid);
    o.slot_utc+=900;o.indoor_valid=true;o.outdoor_valid=false;CHECK(!id.add(o));
    CHECK(id.report(o.slot_utc).last_observation==LearningObservation::OutdoorInvalid);
    o.slot_utc+=900;o.outdoor_valid=true;o.in_model_domain=false;CHECK(!id.add(o));
    CHECK(id.report(o.slot_utc).last_observation==LearningObservation::OutsideDomain);
}

void test_defrost_slots_not_trained() {
    BuildingIdentifier id;
    SlotObservation o{};
    o.slot_utc=t_start;o.indoor_start_c=12;o.indoor_end_c=12.2;
    o.outdoor_c=-2;o.indoor_valid=o.outdoor_valid=o.heat_valid=true;
    o.heat_requested=true;o.setpoint_c=16;o.meter_pump_only=true;
    o.electric_valid=true;o.electric_kwh=.2;
    CHECK(!id.add(o));
    o.slot_utc+=900;CHECK(id.add(o));
    const auto before=id.report(o.slot_utc);
    o.slot_utc+=900;o.defrost_observed=true;o.indoor_end_c=11.8;
    CHECK(!id.add(o));
    const auto after=id.report(o.slot_utc);
    CHECK(after.samples==before.samples && after.revision==before.revision);
    CHECK(after.model.electric_samples==before.model.electric_samples);
    CHECK(after.last_observation==LearningObservation::DefrostObserved);
    CHECK(after.defrost_skipped_this_boot==1);
    o.slot_utc+=900;o.defrost_observed=false;o.heat_requested=false;
    CHECK(!id.add(o)); // no invented stop across defrost
    o.slot_utc+=900;CHECK(id.add(o));
    CHECK(id.report(o.slot_utc).stops==before.stops);
}

void test_no_data_is_honest() {
    BuildingIdentifier id;
    const auto r=id.report(t_start);
    CHECK(r.state==CalibrationState::NoData);
    CHECK(r.samples==0);
    CHECK(!r.model.plausible);
    CHECK(!r.model.cop_identified);
    Parameters p{};bool kwh=true;
    ComfortBand band{};CHECK(mpc_comfort_band(12.0,band));
    CHECK(id.build_parameters(t_start,band,true,1.0,p,kwh)==ModelBuildError::NotCalibrated);
    CHECK(!kwh);
}

void test_comfort_band() {
    ComfortBand b{};
    CHECK(mpc_comfort_band(12.0,b) && std::fabs(b.lower_c-11.5)<1e-9 && std::fabs(b.upper_c-13.0)<1e-9);
    CHECK(mpc_comfort_band(15.0,b) && std::fabs(b.upper_c-15.8)<1e-9);
    CHECK(!mpc_comfort_band(21.0,b));   // occupied: outside HEAT 16 C model
    CHECK(!mpc_comfort_band(16.0,b));
    CHECK(!mpc_comfort_band(NAN,b));
    CHECK(mpc_comfort_band(5.2,b) && b.lower_c>=5.0);
}

void test_standard_gateway_can_calibrate_without_uart_extension_or_meter() {
    Sim sim;
    BuildingIdentifier id;
    CN105GatewayRequestHistory history;
    uint64_t ms=10000;
    unsigned accepted=0;
    for(int i=0;i<12*96;++i) {
        auto o=sim.step(); // simulated independent indoor measurement
        CHECK(history.note(o.heat_requested,16,o.slot_utc,ms,1,1));
        const auto begin=history.snapshot(o.slot_utc,ms,1,1);
        for(unsigned minute=1;minute<=15;++minute)
            CHECK(history.note(o.heat_requested,16,o.slot_utc+minute*60,ms+minute*60000,1,1));
        const auto end=history.snapshot(o.slot_utc+900,ms+900000,1,1);
        o.heat_valid=same_cn105_gateway_request(begin,end) && end.request_minutes>=14;
        o.electric_valid=false;
        o.meter_pump_only=false;
        CHECK(!begin.evidence.source_time_verified && !end.evidence.source_time_verified);
        accepted+=o.heat_valid;
        id.add(o);
        ms+=900000;
    }
    const auto r=id.report(sim.utc);
    std::printf("Standard gateway simulated learning: slots=%u accepted=%u starts=%u stops=%u state=%s\n",
        r.samples,accepted,r.starts,r.stops,calibration_state_name(r.state));
    CHECK(accepted==12*96);
    CHECK(r.state==CalibrationState::Calibrated);
    CHECK(!r.model.electric_trusted && !r.model.cop_identified);
    ComfortBand band{};
    CHECK(mpc_comfort_band(12.0,band));
    Parameters model{};
    bool kwh=true;
    CHECK(id.build_parameters(sim.utc,band,false,1.0,model,kwh)==ModelBuildError::None);
    CHECK(!kwh && valid(model));
}

// Independent minute-level plant, switches at irregular times, not at
// quarter-hour boundaries. Gateway polls bracket the switch, never claim
// a compressor timestamp. The model is still evaluated in 15-min windows.
void test_gateway_mid_slot_switches_can_calibrate() {
    Sim sim;
    BuildingIdentifier id,previous_policy;
    CN105GatewayRequestHistory history;
    bool heat=false;
    unsigned mode_age=120;
    double indoor=12.0,meas=12.0;
    uint64_t ms=10000;
    CHECK(history.note(false,16,sim.utc,ms,1,1));
    unsigned mixed=0,accepted=0;
    for(int i=0;i<16*96;++i) {
        SlotObservation o{};
        o.slot_utc=sim.utc;o.indoor_valid=true;o.indoor_start_c=meas;
        const double to=sim.outdoor(sim.utc);
        o.outdoor_valid=true;o.outdoor_c=to+0.1*sim.rng.normal();
        const auto begin=history.snapshot(sim.utc,ms,1,1);
        CN105SlotCommands command{};
        for(unsigned minute=1;minute<=15;++minute) {
            if(mode_age>=30 && minute!=15 &&
               ((!heat && indoor<11.5)||(heat && indoor>12.5))) {
                heat=!heat;mode_age=0;
                ++command.sent;++command.confirmed;
                command.step=heat?ControlStep::SetHeat:ControlStep::SetOff;
                // The command is dispatched at the start of this minute,
                // and the gateway acknowledges its new request two seconds
                // later. The independently advanced plant sees the change
                // in this minute, not only after the next routine poll.
                command.sent_utc=sim.utc+(minute-1)*60;
                command.ack_utc=command.sent_utc+2;
                command.target_c=16;
                CHECK(history.note(heat,16,command.ack_utc,
                    ms+(minute-1)*60000+2000,1,1));
            }
            const double heating=heat && mode_age>=sim.truth.startup_delay_minutes?
                heat_capacity_kw(sim.truth,to):0.0;
            const double afterheat=!heat && mode_age<15?
                sim.truth.stop_afterheat_kwh/slot_hours:0.0;
            const double a=std::exp(-sim.truth.loss_kw_per_c/(60*sim.truth.thermal_mass_kwh_per_c));
            indoor=to+(indoor-to)*a+(sim.gain_kw+heating+afterheat)/sim.truth.loss_kw_per_c*(1-a);
            ++mode_age;
            CHECK(history.note(heat,16,sim.utc+minute*60,ms+minute*60000,1,1));
        }
        const auto end=history.snapshot(sim.utc+900,ms+900000,1,1);
        o.indoor_end_c=meas=std::round((indoor+0.02*sim.rng.normal())*100.0)/100.0;
        o.heat_requested=heat;o.setpoint_c=16;
        // Reproduce the prior runtime policy, not merely its tracker:
        // every internally commanded mid-slot transition was discarded.
        auto old_observation=o;
        old_observation.heat_valid=command.sent==0 &&
            same_cn105_gateway_request(begin,end) && end.request_minutes>=14;
        previous_policy.add(old_observation);
        const auto input=cn105_learning_request(history,begin,end,command);
        o.heat_valid=input.valid;
        o.transition_before_fraction=input.before_fraction;
        if(input.valid && input.before_fraction>0)++mixed;
        accepted+=o.heat_valid;
        id.add(o);
        sim.utc+=900;ms+=900000;
    }
    const auto old_report=previous_policy.report(sim.utc);
    CHECK(old_report.starts==0 && old_report.stops==0);
    CHECK(old_report.state!=CalibrationState::Calibrated);
    std::printf("Previous runtime: samples=%u starts=%u stops=%u state=%s\n",old_report.samples,old_report.starts,old_report.stops,calibration_state_name(old_report.state));
    const auto r=id.report(sim.utc);
    std::printf("Acknowledged automatic gateway: eligible=%u mixed=%u starts=%u stops=%u windows=%u rms=%.3f state=%s block=%s\n",
        accepted,mixed,r.starts,r.stops,r.windows,r.validation_rms_c,
        calibration_state_name(r.state),calibration_block_name(r.block));
    CHECK(mixed>20 && r.starts>10 && r.stops>10);
    CHECK(r.state==CalibrationState::Calibrated);
    CHECK(r.windows_with_switch>=CalibrationLimits::switch_windows_needed);
    CHECK(!r.model.electric_trusted);
    CHECK(rel(r.model.tau_h,sim.truth.thermal_mass_kwh_per_c/sim.truth.loss_kw_per_c)<0.2);
}

void test_gateway_requests_without_physical_response_do_not_calibrate() {
    Sim sim;
    sim.always_off=true; // physical heating never follows the reported request
    BuildingIdentifier id;
    CN105GatewayRequestHistory history;
    uint64_t ms=10000;
    for(int i=0;i<12*96;++i) {
        auto o=sim.step();
        o.heat_requested=(i/4)%2==0; // gateway can report plausible requests
        CHECK(history.note(o.heat_requested,16,o.slot_utc,ms,1,1));
        const auto begin=history.snapshot(o.slot_utc,ms,1,1);
        for(unsigned minute=1;minute<=15;++minute)
            CHECK(history.note(o.heat_requested,16,o.slot_utc+minute*60,ms+minute*60000,1,1));
        const auto end=history.snapshot(o.slot_utc+900,ms+900000,1,1);
        o.heat_valid=same_cn105_gateway_request(begin,end) && end.request_minutes>=14;
        o.electric_valid=false;
        o.meter_pump_only=false;
        id.add(o);
        ms+=900000;
    }
    const auto r=id.report(sim.utc);
    std::printf("Gateway request without physical heating response: state=%s block=%s\n",
        calibration_state_name(r.state),calibration_block_name(r.block));
    CHECK(r.state!=CalibrationState::Calibrated);
}

void test_recovers_truth_and_calibrates() {
    Sim sim;
    BuildingIdentifier id;
    CalibrationState seen=CalibrationState::NoData;
    bool saw_learning=false,saw_validating=false;
    for(int i=0;i<12*96;++i) {
        id.add(sim.step());
        seen=id.report(sim.utc).state;
        saw_learning|=seen==CalibrationState::Learning;
        saw_validating|=seen==CalibrationState::Validating;
    }
    const auto r=id.report(sim.utc);
    std::printf("tau=%.2f+-%.2f h lift_ref=%.2f+-%.2f K @%.1fC lift0=%.2f delay=%.2f+-%.2f min after=%.3f K "
                "sigma=%.3f rms=%.3f bias=%.3f windows=%u state=%s block=%s\n",
        r.model.tau_h,r.model.tau_std_h,r.model.lift_k_ref,r.model.lift_std_k,
        r.model.lift_ref_outdoor_c,r.model.lift_k_at_0c,
        r.model.start_delay_min,r.model.start_delay_std_min,r.model.afterheat_k,
        r.model.one_step_sigma_c,r.validation_rms_c,r.validation_bias_c,r.windows,
        calibration_state_name(r.state),calibration_block_name(r.block));
    CHECK(saw_learning && saw_validating);
    CHECK(r.state==CalibrationState::Calibrated);
    const double tau_true=sim.truth.thermal_mass_kwh_per_c/sim.truth.loss_kw_per_c;
    CHECK(rel(r.model.tau_h,tau_true)<0.15);
    const double to=r.model.lift_ref_outdoor_c;
    const double lift_true=heat_capacity_kw(sim.truth,to)/sim.truth.loss_kw_per_c;
    CHECK(rel(r.model.lift_k_ref,lift_true)<0.15);
    CHECK(r.model.start_delay_min>=0 && r.model.start_delay_min<=9);
    CHECK(r.model.afterheat_k>=0);
    CHECK(r.model.tau_std_h>0 && r.model.lift_std_k>0);
    CHECK(!r.model.electric_trusted);       // no pump-only meter confirmed
    CHECK(!r.model.absolute_bounds && !r.model.cop_identified);
    // Normalised parameters predict like the truth (open loop, 8 h).
    ComfortBand band{};CHECK(mpc_comfort_band(12.0,band));
    Parameters p{};bool kwh=true;
    CHECK(id.build_parameters(sim.utc,band,false,1.0,p,kwh)==ModelBuildError::None);
    CHECK(!kwh && valid(p));
    State a{12.0,false,120},b=a;
    double worst=0;
    for(int k=0;k<32;++k) {
        const bool on=(k/6)%2==0;
        const double o=-2.0;
        const Period dt{o,0.1,sim.gain_kw,1.0};
        const Period dm{o,0.1,r.model.gain_rise_k*p.loss_kw_per_c,1.0};
        a=advance(sim.truth,a,dt,on).next;
        b=advance(p,b,dm,on).next;
        worst=std::max(worst,std::fabs(a.indoor_c-b.indoor_c));
    }
    std::printf("open-loop 8 h model vs truth worst %.3f K\n",worst);
    CHECK(worst<0.6);
    // One normalised electric unit per HEAT slot without a meter.
    const Period dm{-2.0,0.1,0,1.0};
    const auto heat=advance(p,State{10.0,true,120},dm,true);
    CHECK(std::fabs(heat.electric_kwh-slot_hours)<1e-6);
}

struct ResidentialRun {
    double cost=0,equalized_cost=0,final_c=21;
    double minimum_c=21,maximum_c=21;
    unsigned starts=0,predictive_slots=0;
};
ResidentialRun residential_run(const Sim& sim,const BuildingIdentifier& id,
                               bool use_prices,bool predictive) {
    constexpr ComfortLimits limits{19,23};
    constexpr size_t count=96;
    const auto period=[](size_t i) {
        const size_t hour=(i%96)/4;
        const double spot=hour<6?0.03:hour<16?0.13:hour<20?0.42:0.13;
        const double tariff=hour>=7 && hour<22?0.052:0.028;
        return Period{8+2*std::sin(6.283185307179586*i/96),
            (spot+tariff+0.035)*1.255,0,1};
    };
    State state{21,true,120};
    ResidentialRun metrics{};
    double setpoint=21,mean_price=0;
    HorizonResult forecast{};
    for(size_t i=0;i<count;++i) {
        const int64_t now=sim.utc+static_cast<int64_t>(i)*900;
        const Period actual=period(i);
        mean_price+=actual.total_price_eur_per_kwh/count;
        double prices[16]{};
        for(size_t j=0;j<16;++j)prices[j]=period(i+j).total_price_eur_per_kwh;
        BootstrapInput boot{};boot.heating={16,26,0.5,true,true};
        boot.indoor_fresh=true;boot.indoor_c=state.indoor_c;boot.desired_room_c=21;
        size_t window=8;
        CHECK(apply_profile(boot,OptimizationProfile::Balanced,limits.min_c,limits.max_c,0.10,window));
        boot.next_total_prices=prices;boot.price_count=window;boot.prices_contiguous_and_fresh=use_prices;
        BootstrapDecision decision=bootstrap_price_decision(boot);
        if(predictive) {
            forecast.bridge.status=BridgeStatus::Ready;
            forecast.bridge.first_utc=now;
            forecast.bridge.count=count;
            for(size_t j=0;j<count;++j)forecast.bridge.periods[j]=period(i+j);
            PlannerInput input{};input.pump_limits={16,26,0.5,true,true};
            input.now_utc=now;input.identifier=&id;input.horizon=&forecast;
            input.heating_allowed=input.target_valid=input.temperature_fresh=input.pump_state_known=true;
            input.target_c=21;input.limits=limits;input.indoor_c=state.indoor_c;
            input.pump_heat=state.heat_requested;input.pump_setpoint_c=setpoint;
            input.minutes_in_mode=state.minutes_in_mode;input.pump_limits={16,26,0.5,true,true};
            const auto plan=plan_mpc(input,nullptr);
            ArbiterInput current{};current.pump_limits={16,26,0.5,true,true};
            current.now_utc=now;current.plan=plan.record;current.limits=limits;
            current.calibrated=id.calibrated(now);
            current.temperature_fresh=current.pump_state_known=true;
            current.pump_heat=state.heat_requested;current.pump_setpoint_c=setpoint;
            current.indoor_c=state.indoor_c;current.bootstrap=decision;
            const auto arbitration=arbitrate(current);
            decision=arbitration.proposal;
            if(arbitration.source==ProposalSource::Mpc)++metrics.predictive_slots;
        }
        double requested=setpoint;
        if(decision.action==BootstrapAction::Off)requested=0;
        else if(decision.action==BootstrapAction::HeatSetpoint)requested=decision.heat_setpoint_c;
        if((requested>0)!=state.heat_requested && state.minutes_in_mode<30)requested=setpoint;
        if(requested>0 && !state.heat_requested)++metrics.starts;
        Period plant_period=actual;
        plant_period.internal_gain_kw=sim.gain_kw;
        const auto step=advance_setpoint(sim.truth,state,plant_period,requested);
        state=step.next;setpoint=requested;
        metrics.cost+=step.cost_eur;
        metrics.minimum_c=std::min(metrics.minimum_c,state.indoor_c);
        metrics.maximum_c=std::max(metrics.maximum_c,state.indoor_c);
    }
    metrics.final_c=state.indoor_c;
    const auto ending=period(count-1);
    // Match the plant's final stored heat to the common 21 C state, as in
    // the original maintenance comparison. This is simulation truth only.
    metrics.equalized_cost=metrics.cost+mean_price*sim.truth.thermal_mass_kwh_per_c*
        (21-metrics.final_c)/heat_cop(sim.truth,ending.outdoor_c);
    return metrics;
}

void test_residential_identification_and_planning() {
    Sim sim;
    sim.truth.variable_setpoints=true;
    sim.truth.thermostat_band_c=1.15;
    sim.truth.maximum_indoor_c=24;
    State state{21,false,120};
    BuildingIdentifier id(true);
    for(int i=0;i<16*96;++i,sim.utc+=900) {
        const double to=sim.outdoor(sim.utc)+10;
        const double setpoint=i%20<4?0:i%20<9?21.0:i%20<14?22.5:21.5;
        const Period period{to,0.20,sim.gain_kw,1};
        const Step step=advance_setpoint(sim.truth,state,period,setpoint);
        SlotObservation observed{};
        observed.slot_utc=sim.utc;
        observed.indoor_valid=observed.outdoor_valid=observed.heat_valid=true;
        observed.heat_requested=setpoint>0;
        observed.setpoint_c=setpoint;
        observed.indoor_start_c=state.indoor_c;
        observed.indoor_end_c=step.next.indoor_c;
        observed.outdoor_c=to;
        id.add(observed);
        state=step.next;
    }
    const auto report=id.report(sim.utc);
    std::printf("residential: %s rms=%.3f K samples=%u\n",
        calibration_state_name(report.state),report.validation_rms_c,report.samples);
    CHECK(report.state==CalibrationState::Calibrated && report.validation_rms_c<0.2);
    ComfortBand band{};
    CHECK(mpc_comfort_band(21,band,{19,23},true));
    CHECK(band.lower_c==19 && band.upper_c==23);
    Parameters p{};
    bool kwh=true;
    CHECK(id.build_parameters(sim.utc,band,false,1,p,kwh)==ModelBuildError::None);
    CHECK(p.variable_setpoints && !kwh);
    uint8_t blob[BuildingIdentifier::blob_capacity]{};
    size_t used=0;
    CHECK(id.save(42,blob,sizeof(blob),used));
    BuildingIdentifier restored(true),legacy;
    CHECK(restored.load(42,blob,used) && restored.calibrated(sim.utc));
    CHECK(!legacy.load(42,blob,used));

    HorizonResult forecast{};
    forecast.bridge.status=BridgeStatus::Ready;
    forecast.bridge.first_utc=sim.utc;
    forecast.bridge.count=96;
    for(size_t i=0;i<forecast.bridge.count;++i)
        forecast.bridge.periods[i]={8.0,i>=28 && i<64?0.48:0.08,0,1};
    PlannerInput input{};input.pump_limits={16,26,0.5,true,true};
    input.now_utc=sim.utc;input.identifier=&restored;input.horizon=&forecast;
    input.heating_allowed=input.target_valid=input.temperature_fresh=input.pump_state_known=true;
    input.target_c=21;input.limits={19,23};input.indoor_c=21;
    input.pump_heat=true;input.pump_setpoint_c=21;input.minutes_in_mode=120;
    input.pump_limits={20,23,1,true,true};
    std::vector<PlanRow> rows(max_horizon_slots);
    const auto plan=plan_mpc(input,rows.data());
    CHECK(plan.record.has_plan && plan.rows==forecast.bridge.count);
    CHECK(!plan.energy_in_kwh && std::isnan(plan.cost));
    for(size_t i=0;i<plan.rows;++i) {
        CHECK(!rows[i].heat || input.pump_limits.accepts(rows[i].setpoint_c));
        CHECK(std::isnan(rows[i].energy_kwh) && std::isnan(rows[i].cost));
    }
    ArbiterInput current{};current.pump_limits={16,26,0.5,true,true};
    current.now_utc=sim.utc;current.plan=plan.record;current.limits=input.limits;current.pump_limits=input.pump_limits;
    current.calibrated=current.temperature_fresh=current.pump_state_known=current.pump_heat=true;
    current.pump_setpoint_c=21;current.indoor_c=21;
    current.bootstrap.requested_room_c=21;
    CHECK(arbitrate(current).source==ProposalSource::Mpc);
    current.pump_limits={};
    CHECK(plan_validity(current)==MpcFallback::PumpStateUnknown);
    current.pump_limits=input.pump_limits;
    current.pump_setpoint_c=24;
    CHECK(plan_validity(current)==MpcFallback::PlanInputsChanged);

    input.pump_limits={};
    CHECK(plan_mpc(input,nullptr).record.planning_fallback==MpcFallback::PumpStateUnknown);
    const auto thermostat=residential_run(sim,restored,false,false);
    const auto price=residential_run(sim,restored,true,false);
    const auto mpc=residential_run(sim,restored,true,true);
    const std::pair<const char*,ResidentialRun> comparisons[]{
        {"thermostat",thermostat},{"price",price},{"mpc",mpc}};
    for(const auto& comparison:comparisons) {
        const auto& r=comparison.second;
        std::printf("residential %-10s cost=%.3f EUR equalized=%.3f EUR end=%.2f C range=%.2f..%.2f starts=%u MPC=%u/96\n",
            comparison.first,r.cost,r.equalized_cost,r.final_c,r.minimum_c,r.maximum_c,r.starts,r.predictive_slots);
        CHECK(r.minimum_c>=19 && r.maximum_c<=23 && std::isfinite(r.equalized_cost));
    }
    CHECK(mpc.predictive_slots>0);
}
void test_no_heating_never_calibrates() {
    Sim sim;sim.always_off=true;sim.st.indoor_c=12;sim.meas=12;
    BuildingIdentifier id;
    for(int i=0;i<6*96;++i) id.add(sim.step());
    const auto r=id.report(sim.utc);
    CHECK(r.state==CalibrationState::Learning);
    CHECK(r.block==CalibrationBlock::NeedHeatingSlots);
}

void test_meter_scope_rule() {
    // Whole-house / unconfirmed meter: electric data is never used.
    {
        Sim sim;sim.pump_meter=false;
        BuildingIdentifier id;
        for(int i=0;i<10*96;++i) id.add(sim.step());
        const auto r=id.report(sim.utc);
        CHECK(r.model.electric_samples==0);
        CHECK(!r.model.electric_trusted);
        ComfortBand band{};mpc_comfort_band(12.0,band);
        Parameters p{};bool kwh=true;
        // Even if the CALLER later claims a pump-only meter, no electric
        // model exists to use.
        if(r.state==CalibrationState::Calibrated) {
            CHECK(id.build_parameters(sim.utc,band,true,1.0,p,kwh)==ModelBuildError::None);
            CHECK(!kwh);
        }
    }
    // Owner-confirmed pump-only meter: electric draw identified.
    {
        Sim sim;sim.pump_meter=true;
        BuildingIdentifier id;
        id.select_electric_meter(17);
        for(int i=0;i<12*96;++i) id.add(sim.step());
        const auto r=id.report(sim.utc);
        const double p_true=sim.truth.heat_standby_kw-sim.truth.off_standby_kw+
            heat_capacity_kw(sim.truth,0)/heat_cop(sim.truth,0);
        std::printf("electric on@0C=%.3f (true %.3f) slope=%.4f standby=%.4f start=%.3f rel_err=%.3f "
                    "UA>=%.3f C>=%.3f\n",r.model.electric_on_kw_at_0c,p_true,
                    r.model.electric_on_kw_per_c,r.model.electric_standby_kw,
                    r.model.electric_start_kwh,r.model.electric_rel_error,
                    r.model.ua_lower_kw_per_c,r.model.capacity_lower_kwh_per_c);
        CHECK(r.model.electric_trusted);
        CHECK(rel(r.model.electric_on_kw_at_0c,p_true)<0.10);
        CHECK(r.model.absolute_bounds);
        // A bound must never exceed the truth (COP >= 1 is conservative).
        CHECK(r.model.ua_lower_kw_per_c<=sim.truth.loss_kw_per_c*1.05);
        CHECK(!r.model.cop_identified);
        CHECK(r.state==CalibrationState::Calibrated);
        ComfortBand band{};mpc_comfort_band(12.0,band);
        Parameters p{};bool kwh=false;
        CHECK(id.build_parameters(sim.utc,band,true,1.0,p,kwh)==ModelBuildError::None);
        CHECK(kwh);
        // Meter confirmation withdrawn -> back to normalised units.
        CHECK(id.build_parameters(sim.utc,band,false,1.0,p,kwh)==ModelBuildError::None);
        CHECK(!kwh);
        // Electric energy of one HEAT slot close to the truth at 0 C.
        CHECK(id.build_parameters(sim.utc,band,true,1.0,p,kwh)==ModelBuildError::None);
        const Period d{0.0,0.1,0,1.0};
        const double e_model=advance(p,State{10,true,120},d,true).electric_kwh;
        const double e_true=advance(sim.truth,State{10,true,120},d,true).electric_kwh;
        CHECK(rel(e_model,e_true)<0.10);
        // The confirmed meter changes (or the confirmation is withdrawn):
        // only the electric model restarts, the building model stays.
        const double tau_before=r.model.tau_h;
        const uint32_t samples_before=r.samples;
        std::vector<uint8_t> image(BuildingIdentifier::blob_capacity);
        size_t bytes=0;
        CHECK(id.save(12,image.data(),image.size(),bytes));
        BuildingIdentifier restored;
        CHECK(restored.load(12,image.data(),bytes));
        restored.select_electric_meter(17);
        CHECK(restored.report(sim.utc).model.electric_trusted);
        restored.select_electric_meter(0);
        CHECK(!restored.report(sim.utc).model.electric_trusted);
        CHECK(restored.report(sim.utc).samples==samples_before);
        BuildingIdentifier legacy;
        CHECK(legacy.load(12,image.data(),bytes-8));
        CHECK(legacy.report(sim.utc).samples==samples_before);
        CHECK(!legacy.report(sim.utc).model.electric_trusted);
        id.select_electric_meter(18);
        const auto q=id.report(sim.utc);
        CHECK(q.model.electric_samples==0 && !q.model.electric_trusted && !q.model.absolute_bounds);
        CHECK(std::fabs(q.model.tau_h-tau_before)<1e-9 && q.samples==samples_before);
        CHECK(q.state==CalibrationState::Calibrated);
        CHECK(id.build_parameters(sim.utc,band,true,1.0,p,kwh)==ModelBuildError::None);
        CHECK(!kwh);                       // no trusted electric model -> normalised units
        // It learns the new meter from scratch.
        for(int i=0;i<12*96;++i) id.add(sim.step());
        CHECK(id.report(sim.utc).model.electric_trusted);
    }
}

void test_shared_meter_incremental_model() {
    Sim sim;sim.pump_meter=false;sim.gain_kw=0.17;
    BuildingIdentifier id;
    id.select_electric_meter(23,true);
    CHECK(id.note_baseload_regime(169));
    CHECK(!id.note_baseload_regime(172));
    // Use an independently measured shared meter: subtract its known
    // OFF baseline, including the pump's standby. No heat meter exists.
    for(int i=0;i<12*96;++i) {
        auto o=sim.step();
        o.meter_incremental=true;
        o.electric_kwh=std::max(0.0,o.electric_kwh-sim.truth.off_standby_kw*0.25);
        id.add(o);
    }
    const auto r=id.report(sim.utc);
    CHECK(r.model.electric_incremental && r.model.electric_trusted);
    CHECK(!r.model.absolute_bounds && !r.model.cop_identified);
    CHECK(r.model.baseload_reference_w==169);
    CHECK(r.state==CalibrationState::Calibrated);
    // Room-derived gain includes the miner's heat exactly once.
    CHECK(std::fabs(r.model.gain_rise_k-sim.gain_kw/sim.truth.loss_kw_per_c)<1.5);
    ComfortBand band{};CHECK(mpc_comfort_band(12,band));
    Parameters p{};bool kwh=false;
    CHECK(id.build_parameters(sim.utc,band,false,1,p,kwh,true)==ModelBuildError::None && kwh);
    CHECK(p.heat_standby_kw==0 && p.off_standby_kw==0);
    HorizonResult horizon{};horizon.bridge.status=BridgeStatus::Ready;
    horizon.bridge.first_utc=sim.utc;horizon.bridge.count=48;
    for(size_t i=0;i<horizon.bridge.count;++i)
        horizon.bridge.periods[i]={r.model.lift_ref_outdoor_c,i<24?0.12:0.30,0,1};
    PlannerInput input{};input.pump_limits={16,26,0.5,true,true};input.now_utc=sim.utc;input.identifier=&id;input.horizon=&horizon;
    input.heating_allowed=input.target_valid=input.temperature_fresh=input.pump_state_known=true;
    input.target_c=12;input.indoor_c=12;input.pump_heat=false;input.minutes_in_mode=120;
    input.meter_incremental=true;
    const auto plan=plan_mpc(input,nullptr);
    CHECK(plan.record.has_plan && plan.energy_in_kwh && plan.energy_incremental);
    CHECK(std::isfinite(plan.energy_kwh) && plan.energy_kwh>0);
    CHECK(std::fabs(horizon.bridge.periods[0].internal_gain_kw-
        r.model.gain_rise_k*plan.params.loss_kw_per_c)<1e-9);
    input.meter_incremental=false;
    const auto relative=plan_mpc(input,nullptr);
    CHECK(relative.record.has_plan && !relative.energy_in_kwh && !relative.energy_incremental);
    CHECK(id.build_parameters(sim.utc,band,false,1,p,kwh)==ModelBuildError::None && !kwh);
    CHECK(id.build_parameters(sim.utc,band,true,1,p,kwh)==ModelBuildError::None && !kwh);
    std::vector<uint8_t> bytes(BuildingIdentifier::blob_capacity);size_t used=0;
    CHECK(id.save(12,bytes.data(),bytes.size(),used));
    BuildingIdentifier restored;
    CHECK(restored.load(12,bytes.data(),used));
    restored.select_electric_meter(23,true);
    CHECK(restored.report(sim.utc).model.electric_incremental);
    CHECK(restored.report(sim.utc).model.electric_trusted);
    CHECK(!restored.report(sim.utc).model.absolute_bounds);
    CHECK(restored.report(sim.utc).model.baseload_reference_w==169);
    const auto before=restored.report(sim.utc);
    CHECK(restored.note_baseload_regime(40));
    const auto after=restored.report(sim.utc);
    CHECK(after.samples==before.samples);
    CHECK(after.model.electric_samples==0 && !after.model.electric_trusted);
    CHECK(after.state!=CalibrationState::Calibrated && after.windows==0);
    CHECK(after.model.baseload_reference_w==40);
    CHECK(!restored.note_baseload_regime(41));
    auto damaged=bytes;damaged[used-12]^=0x01;
    CHECK(!restored.load(12,damaged.data(),used));
    restored.select_electric_meter(23,false);
    CHECK(!restored.report(sim.utc).model.electric_incremental &&
          restored.report(sim.utc).model.electric_samples==0);
    // A direct sample must not leak into the shared model.
    BuildingIdentifier separate;separate.select_electric_meter(23,true);
    separate.note_baseload_regime(170);
    sim.pump_meter=true;
    for(int i=0;i<100;++i)separate.add(sim.step());
    CHECK(separate.report(sim.utc).model.electric_samples==0);
}

void test_rejects_replay_gap_and_outlier() {
    Sim sim;
    BuildingIdentifier id;
    std::vector<SlotObservation> obs;
    for(int i=0;i<200;++i) obs.push_back(sim.step());
    for(const auto& o:obs) id.add(o);
    const auto before=id.report(sim.utc);
    CHECK(!id.add(obs.back()));               // duplicate slot
    CHECK(!id.add(obs[10]));                  // replay of older slot
    CHECK(id.report(sim.utc).revision==before.revision);
    SlotObservation gap=sim.step();gap.slot_utc+=1800;sim.utc+=1800;
    CHECK(!id.add(gap));                      // non-contiguous: no start/stop knowledge
    SlotObservation bad=sim.step();bad.slot_utc+=0;bad.slot_utc=0;
    CHECK(!id.add(bad));
    // Recover continuity, then a 2.5 K sensor jump is an outlier.
    for(int i=0;i<4;++i) id.add(sim.step());
    SlotObservation spike=sim.step();
    spike.indoor_end_c=spike.indoor_start_c+2.5;
    const uint32_t out_before=id.report(sim.utc).outliers;
    CHECK(!id.add(spike));
    CHECK(id.report(sim.utc).outliers==out_before+1);
    // NaN never reaches the fit.
    SlotObservation n=sim.step();n.indoor_end_c=NAN;
    CHECK(!id.add(n));
    const auto r=id.report(sim.utc);
    CHECK(std::isfinite(r.model.tau_h));
}

void test_wrong_prior_converges() {
    Sim sim;
    BuildingIdentifier id;
    id.set_prior(4*16.67,3*26.7,50.0);
    for(int i=0;i<14*96;++i) id.add(sim.step());
    const auto r=id.report(sim.utc);
    std::printf("wrong prior: tau=%.2f lift_ref=%.2f state=%s\n",r.model.tau_h,
                r.model.lift_k_ref,calibration_state_name(r.state));
    CHECK(r.state==CalibrationState::Calibrated);
    CHECK(rel(r.model.tau_h,16.67)<0.2);
}

void test_degrades_when_building_changes() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<12*96;++i) id.add(sim.step());
    CHECK(id.report(sim.utc).state==CalibrationState::Calibrated);
    // A window left open: losses x3 (the pump can no longer hold the room).
    // The old model must stop being trusted within one day, whichever
    // check notices first (walk-forward error -> DEGRADED, or the refit
    // becoming implausible/uncertain -> LEARNING).
    sim.truth.loss_kw_per_c*=3.0;
    int dropped_after=-1;
    for(int i=0;i<96 && dropped_after<0;++i) {
        id.add(sim.step());
        if(id.report(sim.utc).state!=CalibrationState::Calibrated) dropped_after=i+1;
    }
    std::printf("open window: calibration withdrawn after %d slots (%s)\n",dropped_after,
                calibration_state_name(id.report(sim.utc).state));
    CHECK(dropped_after>0 && dropped_after<=96);
    ComfortBand band{};mpc_comfort_band(12.0,band);
    Parameters p{};bool kwh=false;
    CHECK(id.build_parameters(sim.utc,band,false,1.0,p,kwh)==ModelBuildError::NotCalibrated);
}

void test_moderate_change_degrades() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<12*96;++i) id.add(sim.step());
    // Heat output halves (e.g. iced coil, wrong mode): pump can still hold
    // the room, the fit keeps excitation, the walk-forward error exposes it.
    sim.truth.heat_kw_at_zero_c*=0.5;sim.truth.minimum_heat_kw*=0.5;
    bool trusted=true;
    for(int i=0;i<2*96 && trusted;++i) {
        id.add(sim.step());
        trusted=id.report(sim.utc).state==CalibrationState::Calibrated;
    }
    std::printf("half capacity: state=%s block=%s\n",
        calibration_state_name(id.report(sim.utc).state),
        calibration_block_name(id.report(sim.utc).block));
    CHECK(!trusted);
}

void test_stale_validation() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<12*96;++i) id.add(sim.step());
    CHECK(id.report(sim.utc).state==CalibrationState::Calibrated);
    const auto r=id.report(sim.utc+15*86400);
    CHECK(r.state==CalibrationState::Validating);
    CHECK(r.block==CalibrationBlock::ValidationStale);
    CHECK(id.report(sim.utc-86400*30).state!=CalibrationState::Calibrated); // clock behind
}

void test_persistence() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<12*96;++i) id.add(sim.step());
    std::vector<uint8_t> blob(BuildingIdentifier::blob_capacity);
    size_t used=0;
    CHECK(id.save(0x1234u,blob.data(),blob.size(),used) && used>0 && used<=blob.size());
    BuildingIdentifier copy;
    CHECK(copy.load(0x1234u,blob.data(),used));
    const auto a=id.report(sim.utc),b=copy.report(sim.utc);
    CHECK(a.state==b.state && a.revision==b.revision && a.samples==b.samples);
    CHECK(std::fabs(a.model.tau_h-b.model.tau_h)<1e-12);
    // Both continue identically.
    const auto o=sim.step();
    id.add(o);copy.add(o);
    CHECK(std::fabs(id.report(sim.utc).model.tau_h-copy.report(sim.utc).model.tau_h)<1e-12);
    BuildingIdentifier other;
    CHECK(!other.load(0x9999u,blob.data(),used));      // other pump/identity
    CHECK(other.report(sim.utc).state==CalibrationState::NoData);
    std::vector<uint8_t> corrupt(blob.begin(),blob.begin()+used);
    corrupt[used/2]^=0x40;
    CHECK(!other.load(0x1234u,corrupt.data(),used));   // CRC
    CHECK(!other.load(0x1234u,blob.data(),used-1));    // size/format
    // Invalid load cannot modify an already learned object.
    const auto before=copy.report(sim.utc);
    CHECK(!copy.load(0x1234u,corrupt.data(),used));
    CHECK(copy.report(sim.utc).samples==before.samples);
    CHECK(copy.report(sim.utc).revision==before.revision);
    CHECK(copy.report(sim.utc).model.tau_h==before.model.tau_h);
    CHECK(!other.save(1,blob.data(),16,used));         // too small buffer
    copy.reset();
    CHECK(copy.report(sim.utc).state==CalibrationState::NoData);
    CHECK(copy.report(sim.utc).samples==0);
    TrendPoint t[1]{};CHECK(copy.trend(t,1)==0);
    BuildingIdentifier residential(true);residential.reset();
    CHECK(residential.variable_setpoints());
    CHECK(residential.report(sim.utc).state==CalibrationState::NoData);
}

void test_trend_ring() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<150;++i) id.add(sim.step());
    std::vector<TrendPoint> t(BuildingIdentifier::trend_slots);
    const size_t n=id.trend(t.data(),t.size());
    CHECK(n==BuildingIdentifier::trend_slots);
    for(size_t i=1;i<n;++i) CHECK(t[i].utc==t[i-1].utc+900);
    CHECK(t[n-1].utc==sim.utc);
}
// Codex round 3 (PR #63): a start/stop is counted only between two slots the
// HEAT 16 C / OFF model accepted.
struct Seq {
    BuildingIdentifier id;
    int64_t utc=t_start;
    double indoor=12.0;
    // One slot; the flags describe what went wrong with it (if anything).
    bool slot(bool heat,bool pump_ok=true,bool indoor_ok=true,bool outdoor_ok=true,
              bool in_domain=true) {
        SlotObservation o{};
        o.slot_utc=utc;
        o.indoor_valid=indoor_ok;
        o.indoor_start_c=indoor;
        o.indoor_end_c=indoor+(heat?0.15:-0.1);
        o.outdoor_valid=outdoor_ok;o.outdoor_c=-2.0;
        o.heat_valid=pump_ok;o.heat_requested=heat;o.in_model_domain=in_domain;
        indoor=o.indoor_end_c;
        if(indoor>13.5) indoor=13.5;
        if(indoor<11.0) indoor=11.0;
        utc+=900;
        return id.add(o);
    }
    uint32_t starts() const {return id.report(utc).starts;}
    uint32_t stops() const {return id.report(utc).stops;}
};

void test_transition_continuity() {
    {   // HEAT 16 C -> OFF with accepted measurements: one start, one stop.
        Seq s;
        s.slot(false);s.slot(false);s.slot(true);s.slot(true);s.slot(false);
        CHECK(s.starts()==1 && s.stops()==1); }
    {   // HEAT at a HIGHER setpoint (outside the model) -> OFF: the OFF slot is
        // not the stop of a heating run the model observed.
        Seq s;
        s.slot(false);s.slot(false);
        s.slot(true,true,true,true,false);      // rejected: not the 16 C model
        s.slot(false);                           // would have been a "stop"
        CHECK(s.stops()==0 && s.starts()==0);
        s.slot(false);s.slot(false);             // chain recovers on its own
        CHECK(s.id.report(s.utc).samples>=2); }
    {   // OFF -> HEAT(higher setpoint) -> HEAT(16 C): the first model-valid
        // HEAT is not a start the model watched.
        Seq s;
        s.slot(false);s.slot(false);
        s.slot(true,true,true,true,false);
        s.slot(true);
        CHECK(s.starts()==0);
        s.slot(false);
        CHECK(s.stops()==1 && s.starts()==0); }   // stop after TWO accepted slots
    {   // Missing / stale pump measurement -> OFF.
        Seq s;
        s.slot(false);s.slot(false);s.slot(true);
        s.slot(true,false);                      // pump data stale for this slot
        s.slot(false);
        CHECK(s.starts()==1 && s.stops()==0); }
    {   // Pump measurement missing on the HEAT slot itself, then OFF.
        Seq s;
        s.slot(false);s.slot(false);
        s.slot(true,false);                      // unknown mode, not HEAT
        s.slot(false);
        CHECK(s.starts()==0 && s.stops()==0); }
    {   // Missing indoor temperature -> HEAT, then OFF.
        Seq s;
        s.slot(false);s.slot(false);
        s.slot(true,true,false);
        s.slot(false);
        CHECK(s.starts()==0 && s.stops()==0); }
    {   // Missing indoor temperature on the OFF slot after a clean HEAT run.
        Seq s;
        s.slot(false);s.slot(false);s.slot(true);s.slot(true);
        s.slot(false,true,false);                // OFF slot unusable
        s.slot(true);                            // next HEAT is not a start
        CHECK(s.starts()==1 && s.stops()==0); }
    {   // A single broken slot (forecast gap) inside a HEAT run: no transition
        // is invented there, and the run stays usable afterwards.
        Seq s;
        s.slot(false);s.slot(false);s.slot(true);
        s.slot(true,true,true,false);            // outdoor missing
        s.slot(true);                            // follows a broken slot: skipped
        s.slot(true);
        CHECK(s.starts()==1 && s.stops()==0);
        s.slot(false);
        CHECK(s.stops()==1); }
    {   // Time gap (missed slots) also breaks the chain.
        Seq s;
        s.slot(false);s.slot(false);s.slot(true);
        s.utc+=3*900;
        s.slot(false);
        CHECK(s.stops()==0); }
    {   // Long-run behaviour: a realistic cycling plant with a periodic
        // out-of-domain HEAT slot and short data breaks still calibrates,
        // is never Degraded, and recovers the true time constant.
        Sim sim;
        BuildingIdentifier id;
        bool ever_degraded=false;
        for(int i=0;i<16*96;++i) {
            SlotObservation o=sim.step();
            if(i%37==11) o.in_model_domain=false;
            if(i%53==20) o.indoor_valid=false;
            if(i%71==30) o.heat_valid=false;
            if(i%89==40) o.outdoor_valid=false;
            id.add(o);
            ever_degraded|=id.report(sim.utc).state==CalibrationState::Degraded;
        }
        const auto r=id.report(sim.utc);
        const double tau_true=sim.truth.thermal_mass_kwh_per_c/sim.truth.loss_kw_per_c;
        std::printf("breaks: tau=%.2f (truth %.2f) state=%s windows=%u\n",r.model.tau_h,tau_true,
            calibration_state_name(r.state),r.windows);
        CHECK(r.state==CalibrationState::Calibrated && !ever_degraded);
        CHECK(rel(r.model.tau_h,tau_true)<0.15);
        CHECK(r.starts>5 && r.stops>5); }
}
} // namespace

int main() {
    test_transition_continuity();
    test_no_data_is_honest();
    test_last_observation_reason();
    test_defrost_slots_not_trained();
    test_comfort_band();
    test_recovers_truth_and_calibrates();
    test_standard_gateway_can_calibrate_without_uart_extension_or_meter();
    test_gateway_mid_slot_switches_can_calibrate();
    test_gateway_requests_without_physical_response_do_not_calibrate();
    test_residential_identification_and_planning();
    test_no_heating_never_calibrates();
    test_meter_scope_rule();
    test_shared_meter_incremental_model();
    test_rejects_replay_gap_and_outlier();
    test_wrong_prior_converges();
    test_degrades_when_building_changes();
    test_moderate_change_degrades();
    test_stale_validation();
    test_persistence();
    test_trend_ring();
    if(failures) {std::printf("%d identification check(s) failed\n",failures);return 1;}
    std::puts("cottage_identification_tests: all passed");
    return 0;
}
