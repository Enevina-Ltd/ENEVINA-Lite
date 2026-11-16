#include "cottage/supervisor.hpp"
#include "cottage/telemetry.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
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
    h.independent_temperature={true,h.now_utc,9.2};
    h.mpc_ready=true;h.plan.status=MpcStatus::Ok;h.plan.count=96;
    h.plan.minimum_c=8.5;h.plan.heat_16_c[0]=0;
    auto d=supervise(p,h);
    assert(d.mode==ControlMode::Shadow);
    assert(d.proposed_mode==ControlMode::PriceOptimization);
    assert(!d.physical_writes_allowed && !d.simulated_heat_16_c);
    h.independent_temperature.indoor_c=7.8;
    d=supervise(p,h);
    assert(d.proposed_mode==ControlMode::FreezeProtection && d.simulated_heat_16_c);
    assert(!d.physical_writes_allowed);
    h.independent_temperature.indoor_c=9.2;
    h.independent_temperature.captured_utc-=181;
    d=supervise(p,h);assert(d.mode==ControlMode::Fault &&
        d.reason==Reason::SensorStale && !d.physical_writes_allowed);
    h.independent_temperature.captured_utc=h.now_utc;
    h.pump.captured_utc=h.now_utc-181;
    d=supervise(p,h);assert(d.mode==ControlMode::Fault &&
        d.reason==Reason::PumpUnavailable);
    h.pump.captured_utc=h.now_utc;
    h.price_fresh=false;h.weather_fresh=false;
    d=supervise(p,h);assert(d.proposed_mode==ControlMode::NormalHeating);
    h.pump.mode=ObservedPumpMode::Off;
    d=supervise(p,h);assert(!d.simulated_heat_16_c);
    h.independent_temperature.indoor_c=8.2;
    d=supervise(p,h);assert(d.simulated_heat_16_c);
    h.watchdog_ok=false;
    d=supervise(p,h);assert(d.mode==ControlMode::Fault);
    h.watchdog_ok=true;h.independent_freeze_protection_verified=false;
    d=supervise(p,h);
    assert(d.reason==Reason::ProtectionNotVerified); // fail closed without independent protection
    h.independent_temperature.indoor_c=10.2;
    d=supervise(p,h);assert(d.reason==Reason::ProtectionNotVerified);
    MitsubishiCN105Adapter mitsubishi;
    assert(mitsubishi.accept_reading(h.pump,h.now_utc));
    assert(!mitsubishi.accept_reading({true,h.now_utc-1000,10,16,0,ObservedPumpMode::Off},h.now_utc));
    ShellyPowerObserver shelly;
    assert(shelly.accept_reading({true,h.now_utc,175,20000},h.now_utc));
    assert(!shelly.accept_reading({true,h.now_utc,-1,20000},h.now_utc));
    assert(!shelly.accept_reading({true,h.now_utc,NAN,20000},h.now_utc));
    auto cmd=simulation_only_command(true);
    assert(cmd.mode==ObservedPumpMode::Heat && cmd.setpoint_c==16);
    cmd=simulation_only_command(false);
    assert(cmd.mode==ObservedPumpMode::Off && cmd.setpoint_c==0);
    std::puts("PASS all telemetry, read-only, and supervisor fault/fallback tests");
}
