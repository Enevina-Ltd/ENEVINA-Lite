#include "cottage/mpc.hpp"
#include "cottage/supervisor.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
using namespace enevina::cottage;
static void check(bool good,const char* reason) {
    if(!good) {std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}
}
static Parameters parameters() {
    Parameters p{};
    p.thermal_mass_kwh_per_c=2;
    p.loss_kw_per_c=.12;
    p.heat_kw_at_zero_c=3.2;
    p.heat_kw_change_per_outdoor_c=.03;
    p.minimum_heat_kw=1;
    p.cop_at_zero_c=2.8;
    p.cop_change_per_outdoor_c=.04;
    p.minimum_cop=1.2;
    p.maximum_cop=5;
    p.startup_delay_minutes=3;
    p.startup_electric_kwh=.05;
    p.heat_standby_kw=.012;
    p.off_standby_kw=.002;
    p.planning_min_c=8;
    p.critical_c=5;
    p.maximum_indoor_c=15.8;
    p.switching_penalty_eur=.03;
    p.min_heat_minutes=60;
    p.min_off_minutes=60;
    return p;
}
struct FakeClock {
    uint64_t time=1000;
    uint64_t step=0;
    unsigned calls=0;
    bool backward=false;
};
static uint64_t fake_ms(void* context) {
    auto& c=*static_cast<FakeClock*>(context);
    ++c.calls;
    if(c.backward && c.calls>=2) return c.time-1;
    const auto now=c.time;
    c.time+=c.step;
    return now;
}
static uint64_t wrapped_32bit_ms(void* context) {
    auto& clock=*static_cast<FakeClock*>(context);
    ++clock.calls;
    const uint32_t reading=static_cast<uint32_t>(clock.time);
    clock.time+=clock.step;
    return reading;
}
static bool atomic_cancel(void* ctx) {
    return static_cast<std::atomic<bool>*>(ctx)->load(std::memory_order_relaxed);
}
struct CancelAfter {
    size_t calls=0;
    size_t limit=std::numeric_limits<size_t>::max();
};
static bool cancel_at(void* ctx) {
    auto& c=*static_cast<CancelAfter*>(ctx);
    return ++c.calls>=c.limit;
}
static void no_plan(const MpcResult& r,MpcStatus status,const char* why) {
    check(r.status==status,why);
    check(r.count==0 && r.cost_eur==0 && r.electric_kwh==0 &&
          r.starts==0 && r.heat_16_c[0]==0,
          "interrupted output must not contain a partial accepted plan");
}
int main() {
    const auto p=parameters();
    const State initial{10,false,180};
    std::vector<Period> periods(96);
    for(size_t t=0;t<periods.size();++t) {
        const double outside=-4+2*std::sin(6.283185307179586*t/96);
        periods[t]={outside,(t%96<20 || t%96>=80)?0.07:0.41,0,1};
    }
    const auto good=optimize(p,initial,periods.data(),periods.size());
    check(good.status==MpcStatus::Ok && good.count==96,
          "normal optimization must succeed with default real steady clock");
    FakeClock deterministic{1000,1,0,false};
    MpcRunControls control{};
    control.runtime_budget_ms=3;
    control.monotonic_now_ms=fake_ms;
    control.clock_context=&deterministic;
    no_plan(optimize(p,initial,periods.data(),periods.size(),3*1024*1024,control),
            MpcStatus::Timeout,"fake clock deadline must interrupt expansion");
    check(deterministic.calls>=4,"fake clock was actually polled");
    std::atomic<bool> stop=true;
    control={};
    control.cancel_requested=atomic_cancel;
    control.cancel_context=&stop;
    no_plan(optimize(p,initial,periods.data(),periods.size(),3*1024*1024,control),
            MpcStatus::Cancelled,"pre-requested cancellation");
    CancelAfter mid{0,500};
    control={};
    control.cancel_requested=cancel_at;
    control.cancel_context=&mid;
    no_plan(optimize(p,initial,periods.data(),periods.size(),3*1024*1024,control),
            MpcStatus::Cancelled,"inner search cancellation");
    check(mid.calls>=mid.limit,"search must poll cancellation inside a slot");
    // A large permitted memory budget must not suppress cancellation.
    mid={0,600};
    no_plan(optimize(p,initial,periods.data(),periods.size(),
            std::numeric_limits<size_t>::max(),control),
            MpcStatus::Cancelled,"huge allowed state budget must stay cancellable");
    // The clock going backwards, including a truncated 32-bit timer wrap,
    // must fail closed instead of incorrectly extending the deadline.
    FakeClock backwards{1000,0,0,true};
    control={};
    control.monotonic_now_ms=fake_ms;
    control.clock_context=&backwards;
    no_plan(optimize(p,initial,periods.data(),periods.size(),3*1024*1024,control),
            MpcStatus::Timeout,"backwards clock failure");
    FakeClock wrapped{std::numeric_limits<uint32_t>::max(),1,0,false};
    control.clock_context=&wrapped;
    // Wrapped timer exposed through a faulty provider should be recognized.
    control.monotonic_now_ms=wrapped_32bit_ms;
    no_plan(optimize(p,initial,periods.data(),periods.size(),3*1024*1024,control),
            MpcStatus::Timeout,"wrap or discontinuous clock failure");
    // Normal timer can be injected and yields the same feasible plan.
    FakeClock advancing{1000,1,0,false};
    control.clock_context=&advancing;
    control.runtime_budget_ms=1000000;
    auto again=optimize(p,initial,periods.data(),periods.size(),3*1024*1024,control);
    check(again.status==MpcStatus::Ok,"recovery after cancellation/timeout");
    check(again.count==good.count && std::abs(again.cost_eur-good.cost_eur)<1e-8,
          "recovered trajectory must be deterministic");
    // Cancel very late (during replay/backtracking), never accept a partial plan.
    CancelAfter total{};
    control={};
    control.cancel_requested=cancel_at;
    control.cancel_context=&total;
    check(optimize(p,initial,periods.data(),periods.size(),3*1024*1024,control).status==
          MpcStatus::Ok,"count complete search cancellation polls");
    CancelAfter late{0,total.calls-1};
    control.cancel_context=&late;
    no_plan(optimize(p,initial,periods.data(),periods.size(),3*1024*1024,control),
            MpcStatus::Cancelled,"late cancellation must reject partial plan");
    // Repeat interruption to check stable recovery/no retained search state.
    for(unsigned i=0;i<20;++i) {
        std::atomic<bool> requested=true;
        MpcRunControls once{};
        once.cancel_requested=atomic_cancel;
        once.cancel_context=&requested;
        no_plan(optimize(p,initial,periods.data(),periods.size(),3*1024*1024,once),
                MpcStatus::Cancelled,"repeated cancellation");
    }
    Health h{};
    h.now_utc=1800000000;
    h.watchdog_ok=true;h.calibrated=true;
    h.independent_freeze_protection_verified=true;
    h.price_fresh=true;h.weather_fresh=true;h.mpc_ready=true;
    h.pump={true,h.now_utc,9.0,16.0,30,ObservedPumpMode::Heat};
    h.pump.evidence={h.now_utc,h.now_utc,true}; // mock confirmed pump-side sample
    h.independent_temperature={true,h.now_utc,9.2};
    h.plan.status=MpcStatus::Timeout;
    auto decision=supervise(p,h);
    check(decision.mode==ControlMode::Shadow &&
          decision.proposed_mode==ControlMode::NormalHeating &&
          decision.reason==Reason::MpcUnavailable &&
          decision.simulated_heat_16_c && !decision.physical_writes_allowed,
          "supervisor timeout must choose simulation-only conservative fallback");
    h.plan.status=MpcStatus::Cancelled;
    decision=supervise(p,h);
    check(decision.reason==Reason::MpcUnavailable &&
          !decision.physical_writes_allowed,
          "supervisor cancellation must stay read-only");
    std::puts("PASS deterministic deadline/cancellation/clock/recovery/supervisor tests");
}
