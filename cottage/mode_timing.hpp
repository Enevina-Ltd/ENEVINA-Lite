#pragma once
#include "cottage/telemetry.hpp"
#include <algorithm>
#include <cstdint>

namespace enevina::cottage {
// Tracks time in the observed CN105 HEAT/OFF mode, not compressor cycles.
// Uptime is monotonic; the wall clock is used only for telemetry freshness.
// On a lost stream/reboot/unknown mode, conservative dwell age is zero.
class ObservedModeDuration {
public:
    void invalidate() {
        known_ = false;
        mode_ = ObservedPumpMode::Unknown;
        started_ms_ = 0;
    }
    void observe(ObservedPumpMode mode, uint64_t now_ms, bool continuity_confirmed) {
        if (mode != ObservedPumpMode::Off && mode != ObservedPumpMode::Heat) {
            invalidate();
            return;
        }
        if (!known_ || mode_ != mode || !continuity_confirmed ||
            now_ms < started_ms_) {
            known_ = true;
            mode_ = mode;
            started_ms_ = now_ms;
        }
    }
    unsigned minutes(ObservedPumpMode current, uint64_t now_ms,
                     bool telemetry_fresh) const {
        if (!telemetry_fresh || !known_ || current != mode_ ||
            now_ms < started_ms_) return 0;
        return static_cast<unsigned>(
            std::min<uint64_t>((now_ms - started_ms_) / 60000u, 600u));
    }
private:
    bool known_ = false;
    ObservedPumpMode mode_ = ObservedPumpMode::Unknown;
    uint64_t started_ms_ = 0;
};
// A stable HEAT setpoint and a stable power/mode have different lifetimes.
// Setpoint changes invalidate a learning interval but do not restart mode dwell.
class ObservedHeatingRequest {
public:
    void observe(PumpRoomMode mode,double setpoint,uint64_t now_ms,bool continuous) {
        const bool known=mode==PumpRoomMode::Off ||
            (mode==PumpRoomMode::Heat && std::isfinite(setpoint));
        if(!continuous || now_ms<mode_since_ || !known_ || !known || mode_!=mode) {
            mode_since_=request_since_=now_ms;
            ++generation_;
        } else if(mode==PumpRoomMode::Heat && std::fabs(setpoint-setpoint_)>0.001) {
            request_since_=now_ms;
            ++generation_;
        }
        known_=known;mode_=mode;setpoint_=setpoint;
    }
    uint32_t generation() const {return generation_;}
    unsigned mode_minutes(uint64_t now_ms) const {return minutes(mode_since_,now_ms);}
    unsigned request_minutes(uint64_t now_ms) const {return minutes(request_since_,now_ms);}
private:
    unsigned minutes(uint64_t since,uint64_t now_ms) const {
        return known_ && now_ms>=since?
            static_cast<unsigned>(std::min<uint64_t>((now_ms-since)/60000,600)):0;
    }
    bool known_=false;
    PumpRoomMode mode_=PumpRoomMode::Unknown;
    double setpoint_=0;
    uint32_t generation_=0;
    uint64_t mode_since_=0,request_since_=0;
};
} // namespace enevina::cottage
