#include "safety/supervisor.hpp"
#include <cmath>

namespace enevina {
Supervisor::Supervisor(bool lab_write_authorized, SupervisorLimits limits):
    lab_write_authorized_(lab_write_authorized), limits_(limits) {}

GateReason Supervisor::review(double proposed, const SafetyInputs& x) {
    if (!lab_write_authorized_) return GateReason::Shadow;
    if (!x.clock_trusted || x.now_utc <= 0 || x.telemetry_utc > x.now_utc)
        return GateReason::ClockError;
    if (!x.pump_connected) return GateReason::Disconnected;
    if (x.now_utc-x.telemetry_utc > limits_.telemetry_max_age_s)
        return GateReason::StaleTelemetry;
    if (x.forecast != ForecastStatus::Ready) return GateReason::StaleForecast;
    if (x.defrosting || x.pump_alarm || x.hygiene_cycle)
        return GateReason::PumpProtection;
    if (x.optimizer_elapsed_ms > limits_.maximum_optimizer_ms)
        return GateReason::OptimizerTimeout;
    if (!x.watchdog_ok) return GateReason::Watchdog;
    if (failed_latched_) return GateReason::FailedCommand;
    if (pending_) return GateReason::AckPending;
    if (!std::isfinite(proposed) || proposed < limits_.minimum_supply_c ||
        proposed > limits_.maximum_supply_c ||
        (have_target_ && std::abs(proposed-last_target_c_) > limits_.maximum_target_step_c))
        return GateReason::OutOfRange;
    if (last_write_utc_ && x.now_utc-last_write_utc_ < limits_.minimum_write_interval_s)
        return GateReason::RateLimited;
    pending_=true;
    last_target_c_=proposed;
    last_write_utc_=x.now_utc;
    have_target_=true;
    return GateReason::Allowed;
}

void Supervisor::acknowledge(double observed, bool success) {
    if (!pending_) return;
    pending_=false;
    if (!success || !std::isfinite(observed) ||
        std::abs(observed-last_target_c_) > 0.5) failed_latched_=true;
}
}
