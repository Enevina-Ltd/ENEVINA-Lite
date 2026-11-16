#include "cottage/supervisor.hpp"
namespace enevina::cottage {
Decision supervise(const Parameters& p,const Health& h) {
    Decision out{};
    // No control path exists, regardless of config or authentication.
    out.mode=ControlMode::Shadow;
    out.proposed_mode=ControlMode::Fault;
    if(!valid(p) || !h.watchdog_ok) {
        out.mode=ControlMode::Fault;
        out.reason=Reason::WatchdogFault;
        return out;
    }
    if(!valid_temperature(h.independent_temperature,h.now_utc)) {
        out.mode=ControlMode::Fault;
        out.reason=Reason::SensorStale;
        return out;
    }
    out.observed_temperature_c=h.independent_temperature.indoor_c;
    if(!valid_cn105(h.pump,h.now_utc)) {
        out.mode=ControlMode::Fault;
        out.reason=Reason::PumpUnavailable;
        return out;
    }
    if(out.observed_temperature_c <= p.planning_min_c ||
        (h.mpc_ready && h.plan.status==MpcStatus::Ok &&
         h.plan.minimum_c <= p.planning_min_c+p.planning_uncertainty_c)) {
        out.proposed_mode=ControlMode::FreezeProtection;
        out.simulated_heat_16_c=true;
        out.reason=Reason::TemperatureLow;
        return out;
    }
    // Without demonstrated independent protection the controller is a
    // diagnostic prototype: it never grants physical write permission.
    if(!h.independent_freeze_protection_verified) {
        out.proposed_mode=ControlMode::NormalHeating;
        out.simulated_heat_16_c=true;
        out.reason=Reason::ProtectionNotVerified;
        return out;
    }
    if(!h.calibrated) {
        out.proposed_mode=ControlMode::NormalHeating;
        out.simulated_heat_16_c=true;
        out.reason=Reason::ModelNotCalibrated;
        return out;
    }
    if(h.price_fresh && h.weather_fresh &&
        h.mpc_ready && h.plan.status==MpcStatus::Ok && h.plan.count>0) {
        out.proposed_mode=ControlMode::PriceOptimization;
        out.simulated_heat_16_c=h.plan.heat_16_c[0]!=0;
        out.reason=Reason::PriceReady;
        return out;
    }
    // Hysteresis in forecast-unavailable fallback: heat below 8.5,
    // continue heat until 9.5. Never send a command; only simulate.
    out.proposed_mode=ControlMode::NormalHeating;
    out.simulated_heat_16_c=out.observed_temperature_c<8.5 ||
        (h.pump.mode==ObservedPumpMode::Heat && out.observed_temperature_c<9.5);
    out.reason=Reason::ForecastUnavailable;
    return out;
}
} // namespace enevina::cottage
