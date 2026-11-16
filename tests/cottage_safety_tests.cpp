#include "cottage/supervisor.hpp"
#include "cottage/telemetry.hpp"
#include <cstdlib>
#include <cmath>
#include <cstdio>
// Always-on safety checks: -DNDEBUG must not bypass validation.
#define check(expression) do { \
    if(!(expression)) { \
        std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#expression); \
        std::exit(1); \
    } \
} while(false)
using namespace enevina::cottage;
int main(){
    Parameters p{};
    p.thermal_mass_kwh_per_c=2;p.loss_kw_per_c=.12;
    p.heat_kw_at_zero_c=3.2;p.minimum_heat_kw=1;
    p.cop_at_zero_c=2.8;p.minimum_cop=1.2;p.maximum_cop=5;
    p.maximum_indoor_c=15.8;
    Health h{};
    h.now_utc=1800000000;h.watchdog_ok=true;h.calibrated=true;
    h.independent_freeze_protection_verified=true;
    h.price_fresh=true;h.weather_fresh=true;
    h.pump={true,h.now_utc,9.0,16.0,30,ObservedPumpMode::Heat};
    h.pump.evidence={h.now_utc,h.now_utc,true}; // mock source proof
    h.independent_temperature={true,h.now_utc,9.2};
    h.mpc_ready=true;h.plan.status=MpcStatus::Ok;h.plan.count=96;
    h.plan.minimum_c=8.5;h.plan.heat_16_c[0]=0;
    auto d=supervise(p,h);
    check(d.mode==ControlMode::Shadow);
    check(d.proposed_mode==ControlMode::PriceOptimization);
    check(!d.physical_writes_allowed && !d.simulated_heat_16_c);
    h.independent_temperature.indoor_c=7.8;
    d=supervise(p,h);
    check(d.proposed_mode==ControlMode::FreezeProtection && d.simulated_heat_16_c);
    check(!d.physical_writes_allowed);
    h.independent_temperature.indoor_c=9.2;
    h.independent_temperature.captured_utc-=181;
    d=supervise(p,h);check(d.mode==ControlMode::Fault &&
        d.reason==Reason::SensorStale && !d.physical_writes_allowed);
    h.independent_temperature.captured_utc=h.now_utc;
    h.pump.captured_utc=h.now_utc-181;
    d=supervise(p,h);check(d.mode==ControlMode::Fault &&
        d.reason==Reason::PumpUnavailable);
    h.pump.captured_utc=h.now_utc;
    h.price_fresh=false;h.weather_fresh=false;
    d=supervise(p,h);check(d.proposed_mode==ControlMode::NormalHeating);
    h.pump.mode=ObservedPumpMode::Off;
    d=supervise(p,h);check(!d.simulated_heat_16_c);
    h.independent_temperature.indoor_c=8.2;
    d=supervise(p,h);check(d.simulated_heat_16_c);
    h.watchdog_ok=false;
    d=supervise(p,h);check(d.mode==ControlMode::Fault);
    h.watchdog_ok=true;h.independent_freeze_protection_verified=false;
    d=supervise(p,h);
    check(d.reason==Reason::ProtectionNotVerified); // fail closed without independent protection
    h.independent_temperature.indoor_c=10.2;
    d=supervise(p,h);check(d.reason==Reason::ProtectionNotVerified);
    MitsubishiCN105Adapter mitsubishi;
    check(mitsubishi.accept_reading(h.pump,h.now_utc));
    check(!mitsubishi.accept_reading({true,h.now_utc-1000,10,16,0,ObservedPumpMode::Off},h.now_utc));
    ShellyPowerObserver shelly;
    check(shelly.accept_reading({true,h.now_utc,175,20000},h.now_utc));
    check(!shelly.accept_reading({true,h.now_utc,-1,20000},h.now_utc));
    check(!shelly.accept_reading({true,h.now_utc,NAN,20000},h.now_utc));
    auto cmd=simulation_only_command(true);
    check(cmd.mode==ObservedPumpMode::Heat && cmd.setpoint_c==16);
    cmd=simulation_only_command(false);
    check(cmd.mode==ObservedPumpMode::Off && cmd.setpoint_c==0);
    // Codex regression: a configurable 12 C floor must not inherit the
    // old hardcoded 8.5/9.5 C fallback. The independent RC model shows
    // why OFF at 12.2 C with -5 C outside would violate the 12 C floor.
    auto raised=p;
    raised.planning_min_c=12;
    raised.planning_uncertainty_c=0.2;
    check(valid(raised));
    const State original{12.2,false,180};
    const Period frost{-5,0.1,0,1};
    check(advance(raised,original,frost,false).next.indoor_c<12);
    Health fallback{};
    fallback.now_utc=h.now_utc;
    fallback.watchdog_ok=true;
    fallback.calibrated=true;
    fallback.independent_freeze_protection_verified=true;
    fallback.pump={true,fallback.now_utc,12.2,16.0,0,ObservedPumpMode::Off};
    fallback.pump.evidence={fallback.now_utc,fallback.now_utc,true};
    fallback.independent_temperature={true,fallback.now_utc,12.2};
    d=supervise(raised,fallback);
    check(d.mode==ControlMode::Shadow &&
        d.proposed_mode==ControlMode::NormalHeating &&
        d.reason==Reason::ForecastUnavailable &&
        d.simulated_heat_16_c && !d.physical_writes_allowed);
    // Configured uncertainty shifts the fallback start as well.
    fallback.independent_temperature.indoor_c=12.6;
    d=supervise(raised,fallback);
    check(d.simulated_heat_16_c && !d.physical_writes_allowed);
    fallback.independent_temperature.indoor_c=12.8;
    d=supervise(raised,fallback);
    check(!d.simulated_heat_16_c);
    fallback.pump.mode=ObservedPumpMode::Heat;
    fallback.pump.compressor_hz=30;
    d=supervise(raised,fallback);
    check(d.simulated_heat_16_c &&
        d.proposed_mode==ControlMode::NormalHeating);
    fallback.independent_temperature.indoor_c=13.8;
    d=supervise(raised,fallback);
    check(!d.simulated_heat_16_c && !d.physical_writes_allowed);
    // A cooling/fan/dry mode must never be silently interpreted as OFF.
    fallback.pump.mode=ObservedPumpMode::Other;
    d=supervise(raised,fallback);
    check(d.mode==ControlMode::Fault && d.reason==Reason::PumpUnavailable &&
          !d.physical_writes_allowed && !d.simulated_heat_16_c);
    std::puts("PASS all telemetry, read-only, and supervisor fault/fallback tests");
}
