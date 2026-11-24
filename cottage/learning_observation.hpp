#pragma once
#include <cstdint>
#include "pump/pump_adapter.hpp"

namespace enevina::cottage {
// Timestamp the independent room readings, not just the optimizer's loop.
// A stale but generally usable thermostat reading must not silently become
// a 15-minute model observation. The allowed jitter is one minute.
inline bool learning_room_boundary(int64_t slot_utc,int64_t captured_utc,int64_t now_utc) {
    return slot_utc>0 && captured_utc>0 && now_utc>=captured_utc &&
        now_utc-captured_utc<=60 && captured_utc>=slot_utc-60 &&
        captured_utc<=slot_utc+60;
}
inline bool learning_interval_span(int64_t begin_utc,int64_t end_utc,
        uint64_t begin_ms,uint64_t end_ms) {
    if(begin_utc<=0 || end_utc<begin_utc || end_ms<begin_ms)return false;
    const int64_t utc_span=end_utc-begin_utc;
    const uint64_t ms_span=end_ms-begin_ms;
    if(utc_span<840 || utc_span>960 || ms_span<840000 || ms_span>960000)return false;
    const int64_t clock_error=utc_span*1000-static_cast<int64_t>(ms_span);
    // A wall-clock step during the interval must not masquerade as elapsed
    // physical time. Allow ordinary timestamp rounding / BLE scan jitter.
    return clock_error>=-5000 && clock_error<=5000;
}
// Do not learn a quiet 15-minute LAN/bus interval from two coincidentally
// identical endpoints after a queued or in-flight command, source change,
// communication discontinuity or uncertain dispatch.
inline bool quiet_common_learning_request(const PumpRoomState& begin,const PumpRoomState& end) {
    return begin.source!=PumpStateSource::None && begin.source==end.source &&
        begin.protocol==end.protocol && begin.endpoint_generation==end.endpoint_generation &&
        begin.link_generation==end.link_generation &&
        begin.configuration_generation==end.configuration_generation &&
        begin.request_generation==end.request_generation &&
        begin.write_generation==end.write_generation &&
        !begin.write_in_progress && !end.write_in_progress &&
        !begin.command_unconfirmed && !end.command_unconfirmed &&
        begin.mode==end.mode && end.request_minutes>=14 &&
        (end.mode==PumpRoomMode::Off ||
         (end.mode==PumpRoomMode::Heat && std::isfinite(begin.setpoint_c) &&
          std::isfinite(end.setpoint_c) && std::fabs(begin.setpoint_c-end.setpoint_c)<=0.001));
}
// At most one checkpoint attempt per hour (four completed slots), except
// the first meaningful sample on a new model. Failures retain the same
// cadence; never hammer the shared NVS when it is short of free entries.
class LearningCheckpoint {
public:
    void reset(bool restored) {attempted_=restored;boundaries_=0;}
    bool due(uint32_t samples,bool dirty) {
        if(boundaries_<4)++boundaries_;
        if(!samples || !dirty || (attempted_ && boundaries_<4))return false;
        attempted_=true;boundaries_=0;return true;
    }
private:
    bool attempted_=false;
    unsigned boundaries_=0;
};
} // namespace enevina::cottage
