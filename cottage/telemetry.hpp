#pragma once
#include <cstdint>
#include "pump/measurement_quality.hpp"
namespace enevina::cottage {
enum class ObservedPumpMode : uint8_t { Unknown, Off, Heat, Other };
struct CN105Telemetry {
    bool received=false;
    int64_t captured_utc=0;
    double indoor_c=0;
    double setpoint_c=0;
    double compressor_hz=0;
    ObservedPumpMode mode=ObservedPumpMode::Unknown;
    enevina::MeasurementEvidence evidence{};
};
struct ShellyTelemetry {
    bool received=false;
    int64_t captured_utc=0;
    double watts=0;
    double total_wh=0; // May include all cottage loads; never pump-only.
};
struct TemperatureSample {
    bool received=false;
    int64_t captured_utc=0;
    double indoor_c=0;
};
bool valid_cn105(const CN105Telemetry& sample,int64_t now_utc,int64_t max_age_s=180);
bool valid_shelly(const ShellyTelemetry& sample,int64_t now_utc,int64_t max_age_s=180);
bool valid_temperature(const TemperatureSample& sample,int64_t now_utc,int64_t max_age_s=180);
// Read-only observable state. This adapter has NO transport writer.
class MitsubishiCN105Adapter final {
public:
    bool accept_reading(const CN105Telemetry& next,int64_t now_utc);
    CN105Telemetry snapshot() const { return sample_; }
private:
    CN105Telemetry sample_{};
};
class ShellyPowerObserver final {
public:
    bool accept_reading(const ShellyTelemetry& next,int64_t now_utc);
    ShellyTelemetry snapshot() const { return sample_; }
private:
    ShellyTelemetry sample_{};
};
struct SimulatedCommand {
    ObservedPumpMode mode=ObservedPumpMode::Off;
    unsigned setpoint_c=0;
};
SimulatedCommand simulation_only_command(bool heat_16_c);
} // namespace enevina::cottage
