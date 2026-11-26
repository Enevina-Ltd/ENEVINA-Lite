#pragma once
#include <cstdint>
#include <optional>
#include "pump/pump_adapter.hpp"
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
    std::optional<bool> defrost;
    std::optional<bool> fault;
};
struct ShellyTelemetry {
    bool received=false;
    int64_t captured_utc=0;
    double watts=0;
    double total_wh=0;
    uint32_t meter_identity=0; // Proven pump-only owner scope of the settings used for this read.
    uint32_t source_identity=0; // Actual configured meter URL + scope, even unconfirmed.
};
struct TemperatureSample {
    bool received=false;
    int64_t captured_utc=0;
    double indoor_c=0;
    // Boot-local evidence for a wired thermostat before clock sync.
    // Never used by price alignment, learning, or UTC history.
    bool monotonic_valid=false;
    uint32_t captured_ms=0;
};
bool valid_cn105(const CN105Telemetry& sample,int64_t now_utc,int64_t max_age_s=180);
bool valid_shelly(const ShellyTelemetry& sample,int64_t now_utc,int64_t max_age_s=180);
bool valid_temperature(const TemperatureSample& sample,int64_t now_utc,int64_t max_age_s=180);
bool valid_local_temperature(const TemperatureSample& sample,uint32_t now_ms,uint32_t max_age_ms=180000);
enum class RoomTemperatureSource : uint8_t { Ruuvi, Pump };
struct RoomTemperatureConfig {
    RoomTemperatureSource source=RoomTemperatureSource::Ruuvi;
    int16_t correction_c100=0;
};
inline bool valid_room_temperature_config(RoomTemperatureConfig c) {
    return (c.source==RoomTemperatureSource::Ruuvi || c.source==RoomTemperatureSource::Pump) &&
           c.correction_c100>=-500 && c.correction_c100<=500;
}
// No implicit source fallback. Correction always applies to the selected
// sensor. The sample retains its measurement time for the age checks.
TemperatureSample local_room_temperature_sample(RoomTemperatureConfig config,
        const TemperatureSample& ruuvi,const PumpRoomState& pump,uint32_t now_ms);
TemperatureSample room_temperature_sample(RoomTemperatureConfig config,
    const TemperatureSample& ruuvi,const PumpRoomState& pump,int64_t now_utc);
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
