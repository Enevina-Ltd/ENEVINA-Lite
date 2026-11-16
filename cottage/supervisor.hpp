#pragma once
#include "cottage/physics.hpp"
#include "cottage/mpc.hpp"
#include "cottage/telemetry.hpp"
#include <cstdint>

namespace enevina::cottage {
enum class ControlMode : uint8_t {
    Shadow, NormalHeating, PriceOptimization, FreezeProtection, Fault
};
enum class Reason : uint8_t {
    ShadowOnly, PriceReady, ForecastUnavailable, TemperatureLow,
    SensorStale, PumpUnavailable, WatchdogFault, MpcUnavailable,
    ModelNotCalibrated, ProtectionNotVerified
};
struct Health {
    int64_t now_utc=0;
    CN105Telemetry pump{};
    TemperatureSample independent_temperature{};
    bool watchdog_ok=false;
    bool calibrated=false;
    bool independent_freeze_protection_verified=false;
    bool price_fresh=false;
    bool weather_fresh=false;
    bool mpc_ready=false;
    MpcResult plan{};
};
struct Decision {
    ControlMode mode=ControlMode::Fault;
    ControlMode proposed_mode=ControlMode::Fault;
    Reason reason=Reason::SensorStale;
    bool simulated_heat_16_c=false;
    bool physical_writes_allowed=false; // Compile-time read-only.
    double observed_temperature_c=0;
};
Decision supervise(const Parameters& p,const Health& health);
} // namespace enevina::cottage
