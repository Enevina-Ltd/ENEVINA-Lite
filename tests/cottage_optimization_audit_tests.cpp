// Fault-path and horizon regressions for the optimization audit
// (docs/OPTIMIZATION_AUDIT_20261118.md). Economic comparisons live in
// tools/cottage_optimization_audit.py; these tests prove that every fault
// yields an explicit status and NO partial plan.
#include "cottage/baseline_policies.hpp"
#include "cottage/finnish_tariff.hpp"
#include "cottage/forecast_bridge.hpp"
#include "cottage/mitsubishi_control_policy.hpp"
#include "cottage/mpc.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace enevina::cottage;

static Parameters model() {
    Parameters p{};
    p.thermal_mass_kwh_per_c=2.0;p.loss_kw_per_c=0.12;
    p.heat_kw_at_zero_c=3.2;p.heat_kw_change_per_outdoor_c=0.03;
    p.minimum_heat_kw=1.0;p.cop_at_zero_c=2.8;p.cop_change_per_outdoor_c=0.04;
    p.minimum_cop=1.2;p.maximum_cop=5;p.startup_delay_minutes=3;
    p.startup_electric_kwh=0.05;p.heat_standby_kw=0.012;p.off_standby_kw=0.002;
    p.planning_min_c=8;p.critical_c=5;p.maximum_indoor_c=14.5;
    p.switching_penalty_eur=0.03;p.min_heat_minutes=60;p.min_off_minutes=60;
    return p;
}
static std::vector<Period> periods(size_t n,double outdoor=-5) {
    std::vector<Period> v(n);
    for(size_t i=0;i<n;++i)
        v[i]={outdoor,(i%96<28||i%96>=88)?0.06:0.30,0,1};
    return v;
}
static void horizons_and_bounds() {
    const auto p=model();
    const State s{10,false,180};
    for(size_t n:{24u,48u,96u,192u}) {
        const auto d=periods(n);
        const auto r=optimize(p,s,d.data(),n);
        assert(r.status==MpcStatus::Ok && r.count==n);
        assert(r.final_c>=10-1e-8 && r.minimum_c>=8-1e-8);
        // An accepted plan is exactly the replayed physics.
        State x=s;double cost=0;
        for(size_t i=0;i<n;++i) {
            const auto st=advance(p,x,d[i],r.heat_16_c[i]!=0);
            x=st.next;cost+=st.cost_eur;
            assert(x.indoor_c>=8-1e-8 && x.indoor_c<=14.5+1e-8);
        }
        assert(std::fabs(cost-r.cost_eur)<1e-9);
        // Baselines obey the same physics and never break dwell limits.
        for(auto policy:{BaselinePolicy::Thermostat,BaselinePolicy::PriceThreshold}) {
            const auto b=simulate_baseline(policy,p,s,d.data(),n);
            assert(b.ok && b.count==n && b.dwell_overrides==0);
            State y=s;unsigned since=180;bool on=false;
            for(size_t i=0;i<n;++i) {
                const bool next=b.heat_16_c[i]!=0;
                if(next!=on) {assert(since>=60);since=15;}
                else since+=15;
                on=next;
                y=advance(p,y,d[i],next).next;
            }
            assert(std::fabs(y.indoor_c-b.final_c)<1e-9);
        }
    }
    const auto too_long=periods(193);
    assert(optimize(p,s,too_long.data(),193).status==MpcStatus::InvalidInput);
}
static void timeout_and_cancel_give_no_plan() {
    const auto p=model();
    const auto d=periods(192);
    MpcRunControls zero{};
    zero.runtime_budget_ms=0;
    auto r=optimize(p,{10,false,180},d.data(),192,3*1024*1024,zero);
    assert(r.status==MpcStatus::Timeout && r.count==0);
    MpcRunControls cancel{};
    cancel.cancel_requested=[](void*){return true;};
    r=optimize(p,{10,false,180},d.data(),192,3*1024*1024,cancel);
    assert(r.status==MpcStatus::Cancelled && r.count==0);
    // Memory budget below one layer: explicit resource error, no plan.
    r=optimize(p,{10,false,180},d.data(),192,1024);
    assert(r.status==MpcStatus::ResourceLimit && r.count==0);
}
static void no_feasible_plan() {
    auto p=model();
    const auto d=periods(96,-20);
    // Start below planning minimum: refuse, no plan.
    auto r=optimize(p,{7.5,false,180},d.data(),96);
    assert(r.status==MpcStatus::UnsafeStart && r.count==0);
    // Pump cannot hold 8 C at -20 C (loss 3.36 kW > capacity 2.6 kW).
    r=optimize(p,{10,false,180},d.data(),96);
    assert(r.status==MpcStatus::Infeasible && r.count==0);
}
static void bridge_faults() {
    const int64_t t0=1768168800; // 2026-01-11 22:00 UTC, quarter-aligned
    std::vector<Spot15> prices;
    for(int i=0;i<96;++i) prices.push_back({t0+900*i,0.05});
    std::vector<enevina::OutdoorPoint> w;
    for(int h=0;h<30;++h) w.push_back({t0+3600*h,-5.0});
    enevina::Tariff day{},night{};
    day.vat_rate=night.vat_rate=0.255;
    day.transfer_eur_per_kwh=0.0345;night.transfer_eur_per_kwh=0.021;
    auto ok=join_forecast_two_tariffs(t0,t0-60,t0-60,prices.data(),prices.size(),
        w.data(),w.size(),day,night,96);
    assert(ok.status==BridgeStatus::Ready && ok.count==96);
    // Missing price slot: explicit Gap, never an interpolated price.
    auto gap=prices;gap.erase(gap.begin()+40);
    auto r=join_forecast_two_tariffs(t0,t0-60,t0-60,gap.data(),gap.size(),
        w.data(),w.size(),day,night,96);
    assert(r.status==BridgeStatus::Gap && r.count==0);
    // Stale weather forecast (> 6 h): no plan.
    r=join_forecast_two_tariffs(t0,t0-60,t0-7*3600,prices.data(),prices.size(),
        w.data(),w.size(),day,night,96);
    assert(r.status==BridgeStatus::StaleWeather && r.count==0);
    // Stale prices (> 36 h): no plan.
    r=join_forecast_two_tariffs(t0,t0-37*3600,t0-60,prices.data(),prices.size(),
        w.data(),w.size(),day,night,96);
    assert(r.status==BridgeStatus::StalePrices && r.count==0);
}
static void dst_quarter_hours() {
    // Autumn 2026: DST ends 25.10. 01:00 UTC. 192 contiguous UTC slots;
    // night transfer 22-07 local switches by local time on both sides.
    const int64_t start=1792875600; // 2026-10-24 21:00 UTC = 00:00 EEST
    std::vector<Spot15> prices;
    for(int i=0;i<192;++i) prices.push_back({start+900*i,0.10});
    std::vector<enevina::OutdoorPoint> w;
    for(int h=0;h<50;++h) w.push_back({start+3600*h,0.0});
    enevina::Tariff day{},night{};
    day.transfer_eur_per_kwh=0.04;night.transfer_eur_per_kwh=0.02;
    const auto r=join_forecast_two_tariffs(start,start-60,start-60,prices.data(),
        prices.size(),w.data(),w.size(),day,night,192);
    assert(r.status==BridgeStatus::Ready && r.count==192);
    // 24.10. local 00:00-07:00 EEST = 21:00-04:00 UTC -> night (28 slots).
    // 25.10. 07:00 EET = 05:00 UTC is the first day slot after DST end.
    for(int i=0;i<192;++i) {
        bool daytime=false;
        assert(finnish_day_transfer(start+900*i,daytime));
        const double expect=daytime?0.14:0.12;
        assert(std::fabs(r.periods[i].total_price_eur_per_kwh-expect)<1e-12);
    }
    bool d=false;
    assert(finnish_day_transfer(1792904400-900,d) && !d); // 04:45 UTC = 06:45 EET
    assert(finnish_day_transfer(1792904400,d) && d);       // 05:00 UTC = 07:00 EET
    assert(finnish_day_transfer(1792951200-900,d) && d);   // 17:45 UTC = 19:45 EET
    assert(finnish_day_transfer(1792958400-900,d) && d);   // 19:45 UTC = 21:45 EET
    assert(finnish_day_transfer(1792958400,d) && !d);      // 20:00 UTC = 22:00 EET
}
static void control_faults() {
    ControlSnapshot s{};
    s.armed=true;s.heating_mode=true;s.temperature_fresh=true;
    s.market_fresh=true;s.cn105_link_fresh=true;s.bridge_get_ok=true;
    s.proposal.action=BootstrapAction::HeatSetpoint;
    s.proposal.heat_setpoint_c=16.0;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::Ready);
    auto lost=s;lost.bridge_get_ok=false; // pump gateway unreachable
    auto out=choose_mitsubishi_control(lost);
    assert(out.gate==ControlBlock::InputsUnverified && out.step==ControlStep::None);
    // Sensor outage: bootstrap is UNAVAILABLE, no command proposed.
    BootstrapInput in{};
    in.indoor_fresh=false;
    const auto d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::Unavailable);
    auto stale=s;stale.proposal=d;stale.temperature_fresh=false;
    assert(choose_mitsubishi_control(stale).step==ControlStep::None);
}
// Codex P1 (PR #30): baselines must PHYSICALLY meet the same terminal as
// MPC/MILP; no hypothetical reheating credit.
static void baselines_meet_equal_terminal() {
    auto p=model();
    const State s{10,false,180};
    for(size_t n:{24u,96u,192u}) {
        const auto d=periods(n,-8);
        for(auto policy:{BaselinePolicy::Thermostat,BaselinePolicy::PriceThreshold}) {
            const auto eq=simulate_baseline_equal_terminal(policy,p,s,d.data(),n);
            assert(eq.result.ok);
            if(eq.terminal_met) assert(eq.result.final_c>=10-1e-9);
            assert(eq.guard_slots>=8 && eq.guard_slots<=n);
        }
    }
    // Physically impossible terminal: reported, never claimed.
    const auto cold=periods(96,-20);
    const auto eq=simulate_baseline_equal_terminal(BaselinePolicy::Thermostat,p,s,cold.data(),96);
    assert(eq.result.ok && !eq.terminal_met && eq.result.final_c<10);
}
int main() {
    baselines_meet_equal_terminal();
    horizons_and_bounds();
    timeout_and_cancel_give_no_plan();
    no_feasible_plan();
    bridge_faults();
    dst_quarter_hours();
    control_faults();
    std::puts("cottage_optimization_audit_tests OK");
}
