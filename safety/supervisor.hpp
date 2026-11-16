#pragma once
#include "forecast/validity.hpp"
#include <cstdint>

namespace enevina {
enum class GateReason {
    Allowed, Shadow, ClockError, Disconnected, StaleTelemetry,
    StaleForecast, PumpProtection, OptimizerTimeout, Watchdog,
    OutOfRange, RateLimited, AckPending, FailedCommand
};
struct SafetyInputs {
    int64_t now_utc = 0;
    int64_t telemetry_utc = 0;
    bool pump_connected = false;
    bool clock_trusted = false;
    ForecastStatus forecast = ForecastStatus::Missing;
    bool defrosting = false;
    bool pump_alarm = false;
    bool hygiene_cycle = false;
    bool watchdog_ok = false;
    uint32_t optimizer_elapsed_ms = 0;
};
struct SupervisorLimits {
    double minimum_supply_c = 25;
    double maximum_supply_c = 55;
    double maximum_target_step_c = 3;
    int64_t telemetry_max_age_s = 120;
    int64_t minimum_write_interval_s = 900;
    uint32_t maximum_optimizer_ms = 2000;
};
// Constructor authorization is for isolated lab testing only. Firmware always
// constructs the default read-only instance until device-specific approval.
class Supervisor {
public:
    explicit Supervisor(bool lab_write_authorized=false,
                        SupervisorLimits limits={});
    GateReason review(double proposed_supply_c, const SafetyInputs& inputs);
    void acknowledge(double observed_supply_c, bool success);
private:
    bool lab_write_authorized_;
    SupervisorLimits limits_;
    bool pending_=false;
    bool failed_latched_=false;
    bool have_target_=false;
    double last_target_c_=0;
    int64_t last_write_utc_=0;
};
}
