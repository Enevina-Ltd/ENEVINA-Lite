// Receding-horizon bridge, MPC arbitration (MPC -> price -> thermostat) and
// the shared planner: stale/partial/gapped data, DST, negative prices, plan
// staleness and every fallback reason.
#include "cottage/building_identification.hpp"
#include "cottage/forecast_bridge.hpp"
#include "cottage/mpc_arbiter.hpp"
#include "cottage/mpc_planner.hpp"
#include "cottage/mitsubishi_control_policy.hpp"
#include "tests/cottage_closed_loop.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace enevina::cottage;

namespace {
int failures=0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); ++failures; } } while(0)

constexpr int64_t now0=1768172400;   // 2026-01-12 01:00 EET, after the training run

struct Data {
    std::vector<Spot15> prices;
    std::vector<enevina::OutdoorPoint> weather;
};
Data make(int64_t now,size_t price_slots,size_t weather_hours,double spot=0.08) {
    Data d;
    const int64_t start=(now/900)*900;
    for(size_t i=0;i<price_slots;++i) d.prices.push_back({start+static_cast<int64_t>(i)*900,spot});
    const int64_t h0=(start/3600)*3600;
    for(size_t h=0;h<weather_hours;++h)
        d.weather.push_back({h0+static_cast<int64_t>(h)*3600,-5.0+0.1*h});
    return d;
}
HorizonResult join(const Data& d,int64_t now,int64_t pf,int64_t wf,size_t min=32) {
    return join_forecast_horizon(now,pf,wf,d.prices.data(),d.prices.size(),
        d.weather.data(),d.weather.size(),closed_loop::sim_pricer,nullptr,192,min);
}

void test_horizon_full_and_partial() {
    {   const auto d=make(now0,200,64);
        const auto h=join(d,now0+30,now0-600,now0-3600);
        CHECK(h.bridge.status==BridgeStatus::Ready && h.bridge.count==192);
        CHECK(h.limit==HorizonLimit::MaxSlots);
        CHECK(h.bridge.first_utc==now0);
        CHECK(std::fabs(h.bridge.periods[4].outdoor_c-(-4.9))<1e-9); // 01:00 point
        CHECK(std::fabs(h.bridge.periods[0].total_price_eur_per_kwh-
              closed_loop::total_price(now0,0.08))<1e-12); }
    {   // Prices only until tonight: shorter horizon, not a failure.
        const auto d=make(now0,80,64);
        const auto h=join(d,now0,now0-600,now0-3600);
        CHECK(h.bridge.status==BridgeStatus::Ready && h.bridge.count==80);
        CHECK(h.limit==HorizonLimit::PriceEnd); }
    {   // Missing price slot in the middle ends the horizon at the gap.
        auto d=make(now0,192,64);
        d.prices.erase(d.prices.begin()+50);
        const auto h=join(d,now0,now0-600,now0-3600);
        CHECK(h.bridge.status==BridgeStatus::Ready && h.bridge.count==50);
        CHECK(h.limit==HorizonLimit::PriceGap); }
    {   // A gap too early: no plan.
        auto d=make(now0,192,64);
        d.prices.erase(d.prices.begin()+10);
        const auto h=join(d,now0,now0-600,now0-3600);
        CHECK(h.bridge.status==BridgeStatus::Gap && h.bridge.count==0); }
    {   // Duplicated slot = corrupt cache, ends the horizon there.
        auto d=make(now0,192,64);
        d.prices.push_back(d.prices[60]);
        const auto h=join(d,now0,now0-600,now0-3600);
        CHECK(h.bridge.count==60 && h.limit==HorizonLimit::PriceGap); }
    {   // Weather shorter than prices.
        const auto d=make(now0,192,30);
        const auto h=join(d,now0,now0-600,now0-3600);
        CHECK(h.bridge.status==BridgeStatus::Ready && h.bridge.count==29*4+1);
        CHECK(h.limit==HorizonLimit::WeatherEnd); }
    {   // Missing forecast hour: no interpolation across it.
        auto d=make(now0,192,64);
        d.weather.erase(d.weather.begin()+20);
        const auto h=join(d,now0,now0-600,now0-3600);
        CHECK(h.bridge.status==BridgeStatus::Ready && h.bridge.count==19*4+1);
        CHECK(h.limit==HorizonLimit::WeatherGap); }
    {   // Weather starting after the current slot cannot cover it.
        auto d=make(now0,192,64);
        d.weather.erase(d.weather.begin());
        const auto h=join(d,now0,now0-600,now0-3600);
        CHECK(h.bridge.status!=BridgeStatus::Ready); }
}

void test_horizon_stale_and_clock() {
    const auto d=make(now0,192,64);
    CHECK(join(d,now0,now0-37*3600,now0).bridge.status==BridgeStatus::StalePrices);
    CHECK(join(d,now0,now0+60,now0).bridge.status==BridgeStatus::StalePrices);  // future fetch
    CHECK(join(d,now0,now0,now0-7*3600).bridge.status==BridgeStatus::StaleWeather);
    CHECK(join(d,now0+120,now0,now0).bridge.status==BridgeStatus::UnalignedStart);
    CHECK(join(d,1000,0,0).bridge.status==BridgeStatus::BadClock);
}

void test_horizon_negative_and_dst() {
    {   const auto d=make(now0,192,64,-0.20);
        const auto h=join(d,now0,now0,now0);
        CHECK(h.bridge.status==BridgeStatus::Ready && h.bridge.count==192);
        double expected=0;
        enevina::calculate_total_price(closed_loop::tariff_for(now0,-0.20),expected);
        CHECK(h.bridge.periods[0].total_price_eur_per_kwh<0);
        CHECK(std::fabs(h.bridge.periods[0].total_price_eur_per_kwh-expected)<1e-12); }
    {   // Autumn DST 2026-10-25 (100 local slots): UTC slots stay contiguous and
        // the night/day transfer boundary moves with local time.
        const int64_t t=1792879200; // 2026-10-24 22:00 UTC = 25.10. 01:00 EEST
        const auto d=make(t,192,64,0.05);
        const auto h=join(d,t,t,t);
        CHECK(h.bridge.status==BridgeStatus::Ready && h.bridge.count==192);
        // 07:00 local after the change is 05:00 UTC: day transfer starts there.
        const size_t i_day=static_cast<size_t>((1792904400-t)/900);
        CHECK(h.bridge.periods[i_day].total_price_eur_per_kwh>
              h.bridge.periods[i_day-1].total_price_eur_per_kwh); }
}

BootstrapDecision price_decision(BootstrapAction a,bool prices=true) {
    BootstrapDecision d{};
    d.action=a;d.price_data_valid=prices;d.requested_room_c=12.0;
    d.reason=a==BootstrapAction::HeatSetpoint?BootstrapReason::CheapWindow:
             BootstrapReason::ExpensiveWindow;
    if(a==BootstrapAction::HeatSetpoint) d.heat_setpoint_c=17.0;
    return d;
}
MpcPlanRecord good_plan(bool first_heat) {
    MpcPlanRecord p{};
    p.has_plan=true;p.status=MpcStatus::Ok;p.count=96;
    p.first_utc=now0;p.computed_utc=now0+20;
    p.initial_heat=false;p.first_heat=first_heat;
    p.target_c=12.0;mpc_comfort_band(12.0,p.band);
    p.planning_fallback=MpcFallback::None;
    return p;
}
ArbiterInput base_input() {
    ArbiterInput in{};
    in.now_utc=now0+40;in.temperature_fresh=true;in.indoor_c=12.0;
    in.calibrated=true;in.calibration=CalibrationState::Calibrated;
    in.plan=good_plan(true);
    in.pump_state_known=true;in.pump_heat=false;
    in.bootstrap=price_decision(BootstrapAction::Off);
    return in;
}

void test_arbiter_order_and_fallbacks() {
    {   auto in=base_input();
        const auto o=arbitrate(in);
        CHECK(o.source==ProposalSource::Mpc && o.mpc_state==MpcRunState::Active);
        CHECK(o.proposal.action==BootstrapAction::HeatSetpoint);
        CHECK(std::fabs(o.proposal.heat_setpoint_c-16.0)<1e-9);
        CHECK(!o.proposal.executable); }
    {   auto in=base_input();in.plan=good_plan(false);
        const auto o=arbitrate(in);
        CHECK(o.source==ProposalSource::Mpc && o.proposal.action==BootstrapAction::Off); }
    {   auto in=base_input();in.calibrated=false;in.calibration=CalibrationState::Learning;
        const auto o=arbitrate(in);
        CHECK(o.source==ProposalSource::PriceControl && o.mpc_state==MpcRunState::Calibrating);
        CHECK(o.fallback==MpcFallback::ModelLearning);
        CHECK(o.proposal.action==BootstrapAction::Off); }
    {   auto in=base_input();in.calibrated=false;in.calibration=CalibrationState::Validating;
        CHECK(arbitrate(in).fallback==MpcFallback::ModelValidating); }
    {   auto in=base_input();in.calibrated=false;in.calibration=CalibrationState::Degraded;
        const auto o=arbitrate(in);
        CHECK(o.mpc_state==MpcRunState::Fallback && o.fallback==MpcFallback::ModelDegraded); }
    {   // Price data missing: the bootstrap is a thermostat; still the fallback.
        auto in=base_input();in.calibrated=false;
        in.bootstrap=price_decision(BootstrapAction::HeatSetpoint,false);
        CHECK(arbitrate(in).source==ProposalSource::Thermostat); }
    {   auto in=base_input();in.now_utc=now0+900;   // next slot: stale
        CHECK(arbitrate(in).fallback==MpcFallback::PlanStale);
        CHECK(arbitrate(in).source==ProposalSource::PriceControl); }
    {   auto in=base_input();in.now_utc=now0-10;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanStale); }
    {   auto in=base_input();in.plan.computed_utc=in.now_utc+5;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanStale); }
    {   auto in=base_input();in.config_generation=7;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanInputsChanged); }
    {   auto in=base_input();in.mode_generation=3;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanInputsChanged); }
    {   auto in=base_input();in.settings_generation=2;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanInputsChanged); }
    // Codex P1 (PR #63): no verified pump-origin state -> no MPC decision.
    {   auto in=base_input();in.pump_state_known=false;
        CHECK(arbitrate(in).fallback==MpcFallback::PumpStateUnknown);
        CHECK(arbitrate(in).source==ProposalSource::PriceControl); }
    // Codex P2 (PR #63): the plan's OWN executed transition advances the
    // mode generation; with a confirmed readback and the verified pump in the
    // planned state the plan stays in force for its slot.
    {   auto in=base_input();in.mode_generation=1;in.pump_heat=true;
        in.own_transition_confirmed=true;
        const auto o=arbitrate(in);
        CHECK(o.source==ProposalSource::Mpc && o.proposal.action==BootstrapAction::HeatSetpoint); }
    {   // Same generation advance without a confirmed readback: re-plan.
        auto in=base_input();in.mode_generation=1;in.pump_heat=true;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanInputsChanged); }
    {   // Confirmed, but the verified pump is NOT in the planned state.
        auto in=base_input();in.mode_generation=1;in.pump_heat=false;
        in.own_transition_confirmed=true;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanInputsChanged); }
    {   // A plan that keeps the mode cannot explain a generation change.
        auto in=base_input();in.plan=good_plan(false);in.mode_generation=1;
        in.own_transition_confirmed=true;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanInputsChanged); }
    {   auto in=base_input();in.bootstrap.requested_room_c=13.0;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanInputsChanged); }
    {   // Plan from OFF, decided OFF; pump found HEAT (manual): re-plan.
        auto in=base_input();in.plan=good_plan(false);in.pump_heat=true;
        CHECK(arbitrate(in).fallback==MpcFallback::PlanInputsChanged); }
    {   // Plan from OFF decided HEAT; after executing it the pump is HEAT: fine.
        auto in=base_input();in.pump_heat=true;
        CHECK(arbitrate(in).source==ProposalSource::Mpc); }
    {   auto in=base_input();
        in.bootstrap.action=BootstrapAction::Off;
        in.bootstrap.reason=BootstrapReason::NoHeatingRequested;
        const auto o=arbitrate(in);
        CHECK(o.fallback==MpcFallback::NoHeatingMode && o.proposal.action==BootstrapAction::Off); }
    {   auto in=base_input();in.temperature_fresh=false;
        in.bootstrap=BootstrapDecision{};   // bootstrap also unavailable
        const auto o=arbitrate(in);
        CHECK(o.fallback==MpcFallback::TemperatureStale && o.source==ProposalSource::None);
        CHECK(o.proposal.action==BootstrapAction::Unavailable); }
    {   auto in=base_input();in.plan=good_plan(false);in.indoor_c=11.2;
        CHECK(arbitrate(in).fallback==MpcFallback::BelowComfortFloor); }
    {   auto in=base_input();in.indoor_c=13.4;
        CHECK(arbitrate(in).fallback==MpcFallback::AboveComfortCeiling); }
    {   auto in=base_input();in.plan.has_plan=false;
        in.plan.planning_fallback=MpcFallback::OutdoorOutsideLearned;
        CHECK(arbitrate(in).fallback==MpcFallback::OutdoorOutsideLearned); }
    for(auto s:{MpcStatus::Timeout,MpcStatus::Cancelled})
        CHECK(fallback_for_status(s)==MpcFallback::SolverTimeout);
    CHECK(fallback_for_status(MpcStatus::Infeasible)==MpcFallback::SolverFailed);
    CHECK(fallback_for_status(MpcStatus::UnsafeStart)==MpcFallback::BelowComfortFloor);
    {   auto in=base_input();in.plan.status=MpcStatus::Infeasible;
        CHECK(arbitrate(in).fallback==MpcFallback::SolverFailed); }
}

void test_arbiter_feeds_existing_policy() {
    // The MPC decision goes through the UNCHANGED Mitsubishi policy: arming,
    // freshness, setpoint validity and the 30-min mode dwell still apply.
    const auto o=arbitrate(base_input());
    ControlSnapshot s{};
    s.armed=false;s.heating_mode=true;s.temperature_fresh=true;s.market_fresh=true;
    s.cn105_link_fresh=true;s.bridge_get_ok=true;
    s.current_heat=false;s.current_setpoint_c=16.0;s.proposal=o.proposal;
    s.ms_since_mode_change=mitsubishi_min_mode_dwell_ms;
    CHECK(choose_mitsubishi_control(s).step==ControlStep::None);   // not armed
    s.armed=true;
    CHECK(choose_mitsubishi_control(s).step==ControlStep::SetHeat);
    s.ms_since_mode_change=60000;
    CHECK(choose_mitsubishi_control(s).gate==ControlBlock::ModeDwell);
    s.ms_since_mode_change=mitsubishi_min_mode_dwell_ms;s.current_setpoint_c=20.0;
    const auto c=choose_mitsubishi_control(s);
    CHECK(c.step==ControlStep::Setpoint && std::fabs(c.target_c-16.0)<1e-9);
}

// --- planner ------------------------------------------------------------
BuildingIdentifier trained(bool meter=false) {
    closed_loop::Scenario sc=closed_loop::scenarios()[5]; // flat prices
    sc.meter_pump_only=meter;sc.eval_slots=4;
    closed_loop::Harness h(sc);
    BuildingIdentifier id;
    h.run(closed_loop::Kind::Mpc,&id);
    return id;
}
PlannerInput planner_input(const BuildingIdentifier& id,HorizonResult& hz,int64_t now) {
    PlannerInput pi{};
    pi.now_utc=now;pi.identifier=&id;pi.currency_scale=1.0;
    pi.heating_allowed=true;pi.target_valid=true;pi.target_c=12.0;
    pi.temperature_fresh=true;pi.indoor_c=12.0;
    pi.pump_state_known=true;pi.pump_heat=false;pi.minutes_in_mode=120;
    pi.horizon=&hz;
    pi.controls.runtime_budget_ms=60000;
    return pi;
}

void test_planner() {
    const BuildingIdentifier none;
    const BuildingIdentifier id=trained();
    const auto rep=id.report(now0);
    std::printf("trained identifier: %s, outdoor range %.1f..%.1f C\n",
        calibration_state_name(rep.state),rep.model.outdoor_min_c,rep.model.outdoor_max_c);
    CHECK(rep.state==CalibrationState::Calibrated);
    auto d=make(now0,192,64);
    for(auto& w:d.weather) w.temperature_c=-3.0;
    auto fresh=[&]() {return join(d,now0,now0-600,now0-600);};
    {   auto hz=fresh();auto pi=planner_input(none,hz,now0);
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::ModelLearning); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);
        std::vector<PlanRow> rows(max_horizon_slots);
        const auto o=plan_mpc(pi,rows.data());
        CHECK(o.record.has_plan && o.record.status==MpcStatus::Ok && o.record.count==192);
        CHECK(o.rows==192 && rows[0].utc==now0 && rows[191].utc==now0+191*900);
        CHECK(!o.energy_in_kwh && std::isnan(o.energy_kwh) && std::isnan(rows[0].cost));
        double lowest=1e9;
        for(size_t i=0;i<o.rows;++i) lowest=std::min(lowest,rows[i].indoor_c);
        CHECK(lowest>=o.params.planning_min_c+o.params.planning_uncertainty_c-1e-6);
        CHECK(std::fabs(o.params.planning_min_c+o.params.planning_uncertainty_c-o.record.band.lower_c)<1e-9);
        CHECK(o.record.first_utc==now0 && o.record.computed_utc==now0); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);pi.target_c=21.0;
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::TargetOutsideModel); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);pi.heating_allowed=false;
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::NoHeatingMode); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);pi.pump_state_known=false;
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::PumpStateUnknown); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);pi.temperature_fresh=false;
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::TemperatureStale); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);pi.indoor_c=10.5;
        const auto o=plan_mpc(pi,nullptr);
        CHECK(!o.record.has_plan && o.record.planning_fallback==MpcFallback::BelowComfortFloor); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);pi.indoor_c=14.0;
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::AboveComfortCeiling); }
    {   HorizonResult unused{};auto pi=planner_input(id,unused,now0);pi.horizon=nullptr;
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::ForecastUnavailable); }
    {   auto d2=make(now0,20,64);
        auto hz=join(d2,now0,now0,now0,8);    // Ready but shorter than 32
        auto pi=planner_input(id,hz,now0);
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::HorizonTooShort); }
    {   auto d2=d;for(auto& w:d2.weather) w.temperature_c=-30.0;
        auto hz=join(d2,now0,now0,now0);auto pi=planner_input(id,hz,now0);
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::OutdoorOutsideLearned); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);pi.controls.runtime_budget_ms=0;
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::SolverTimeout); }
    {   auto hz=fresh();auto pi=planner_input(id,hz,now0);pi.memory_budget_bytes=1024;
        CHECK(plan_mpc(pi,nullptr).record.planning_fallback==MpcFallback::ResourceLimit); }
    {   // Plan bound to its slot: a horizon from another slot is refused.
        auto hz=fresh();auto pi=planner_input(id,hz,now0+900);
        CHECK(!plan_mpc(pi,nullptr).record.has_plan); }
    {   const BuildingIdentifier metered=trained(true);
        auto hz=fresh();auto pi=planner_input(metered,hz,now0);pi.meter_pump_only=true;
        std::vector<PlanRow> rows(max_horizon_slots);
        const auto o=plan_mpc(pi,rows.data());
        CHECK(o.record.has_plan && o.energy_in_kwh);
        CHECK(std::isfinite(o.energy_kwh) && o.energy_kwh>0 && std::isfinite(o.cost));
        // Withdrawn confirmation: no kWh/currency claims.
        pi.meter_pump_only=false;auto hz2=fresh();pi.horizon=&hz2;
        CHECK(!plan_mpc(pi,nullptr).energy_in_kwh); }
}

void test_afterheat_is_consistent() {
    // MPC search and exact replay use the same after-heat term.
    Parameters p=closed_loop::true_cottage();
    p.planning_min_c=8.0;p.maximum_indoor_c=14.5;p.min_heat_minutes=60;p.min_off_minutes=60;
    p.switching_penalty_eur=0.02;p.stop_afterheat_kwh=0.3;
    std::vector<Period> periods(96,Period{-5.0,0.1,0.0,1.0});
    for(size_t i=40;i<60;++i) periods[i].total_price_eur_per_kwh=0.9;
    const auto r=optimize(p,State{10.0,false,120},periods.data(),periods.size());
    CHECK(r.status==MpcStatus::Ok);
    State s{10.0,false,120};
    for(size_t i=0;i<r.count;++i) s=advance(p,s,periods[i],r.heat_16_c[i]!=0).next;
    CHECK(std::fabs(s.indoor_c-r.final_c)<1e-9);
    Parameters q=p;q.stop_afterheat_kwh=0;
    const auto a=advance(p,State{12.0,true,60},periods[0],false);
    const auto b=advance(q,State{12.0,true,60},periods[0],false);
    CHECK(a.next.indoor_c>b.next.indoor_c);
    CHECK(std::fabs(a.electric_kwh-b.electric_kwh)<1e-12);   // free heat, no kWh
    p.stop_afterheat_kwh=2.5;CHECK(!valid(p));
    p.stop_afterheat_kwh=-0.1;CHECK(!valid(p));
}
} // namespace

int main() {
    test_horizon_full_and_partial();
    test_horizon_stale_and_clock();
    test_horizon_negative_and_dst();
    test_arbiter_order_and_fallbacks();
    test_arbiter_feeds_existing_policy();
    test_planner();
    test_afterheat_is_consistent();
    if(failures) {std::printf("%d MPC control check(s) failed\n",failures);return 1;}
    std::puts("cottage_mpc_control_tests: all passed");
    return 0;
}
