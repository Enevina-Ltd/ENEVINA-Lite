#include "cottage/telemetry.hpp"
#include <cmath>
namespace enevina::cottage {
namespace {
bool age_ok(int64_t stamp,int64_t now,int64_t limit) {
    return stamp>0 && now>=stamp && now-stamp<=limit && limit>0;
}
}
bool valid_cn105(const CN105Telemetry& s,int64_t now,int64_t age) {
    return s.received && enevina::verified_fresh(s.evidence,now,age) &&
        age_ok(s.captured_utc,now,age) &&
        std::isfinite(s.indoor_c) && s.indoor_c>=-10 && s.indoor_c<=50 &&
        std::isfinite(s.setpoint_c) && s.setpoint_c>=0 && s.setpoint_c<=35 &&
        std::isfinite(s.compressor_hz) && s.compressor_hz>=0 &&
        s.compressor_hz<=200 && s.mode!=ObservedPumpMode::Unknown;
}
bool valid_shelly(const ShellyTelemetry& s,int64_t now,int64_t age) {
    return s.received && age_ok(s.captured_utc,now,age) &&
        std::isfinite(s.watts) && s.watts>=0 && s.watts<20000 &&
        std::isfinite(s.total_wh) && s.total_wh>=0;
}
bool valid_temperature(const TemperatureSample& s,int64_t now,int64_t age) {
    return s.received && age_ok(s.captured_utc,now,age) &&
        std::isfinite(s.indoor_c) && s.indoor_c>=-10 && s.indoor_c<=45;
}
TemperatureSample room_temperature_sample(RoomTemperatureConfig config,
        const TemperatureSample& ruuvi,const PumpRoomState& pump,int64_t now_utc) {
    if(!valid_room_temperature_config(config))return {};
    TemperatureSample sample=ruuvi;
    if(config.source==RoomTemperatureSource::Pump) {
        int64_t captured=0;
        if(pump.source==PumpStateSource::VerifiedBus &&
           enevina::verified_fresh(pump.evidence,now_utc,180))
            captured=pump.evidence.source_utc;
        else if(pump.source==PumpStateSource::DirectLan &&
                age_ok(pump.evidence.received_utc,now_utc,90))
            captured=pump.evidence.received_utc;
        sample={captured>0,captured,pump.indoor_c};
    }
    if(!valid_temperature(sample,now_utc))return {};
    sample.indoor_c+=static_cast<double>(config.correction_c100)/100.0;
    return valid_temperature(sample,now_utc)?sample:TemperatureSample{};
}
bool MitsubishiCN105Adapter::accept_reading(const CN105Telemetry& next,int64_t now) {
    if(!valid_cn105(next,now))return false;
    sample_=next;
    return true;
}
bool ShellyPowerObserver::accept_reading(const ShellyTelemetry& next,int64_t now) {
    if(!valid_shelly(next,now))return false;
    sample_=next;
    return true;
}
SimulatedCommand simulation_only_command(bool heat_16_c) {
    return {heat_16_c?ObservedPumpMode::Heat:ObservedPumpMode::Off,
        heat_16_c?16u:0u};
}
} // namespace enevina::cottage
