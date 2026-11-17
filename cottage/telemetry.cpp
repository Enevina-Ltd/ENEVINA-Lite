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
