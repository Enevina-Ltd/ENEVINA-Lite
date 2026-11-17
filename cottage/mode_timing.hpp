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
} // namespace enevina::cottage
