#pragma once
#include "pump/pump_adapter.hpp"
#include <cmath>
#include <cstdint>
#include <limits>

namespace enevina::cottage {
// One uncertain automatic dispatch per active authorization. A failed or
// rejected SET is not permission to send it again at the next optimizer tick.
// Ordinary polls continue. Only an independently observed matching state, or
// an explicit new authorization, permits the next automatic command. The
// observation does NOT establish causal command acknowledgement for learning.
class AutomaticCommandReconciliation {
public:
    void unresolved(uint32_t authorization,PumpRoomMode expected_mode,
                    bool target_required,double target_c=0) {
        pending_=true;authorization_=authorization;mode_=expected_mode;
        target_required_=target_required;target_=target_c;
    }
    bool awaiting(uint32_t authorization)const {
        return pending_ && authorization_==authorization;
    }
    // A dispatch decision must not consume cached state. Only a completed
    // independent poll can resolve an uncertain command. Return true ONLY
    // when that poll actually clears the current authorization's receipt.
    bool observe(uint32_t authorization,bool fresh,PumpRoomMode mode,
                 double target_c=std::numeric_limits<double>::quiet_NaN()) {
        if(!awaiting(authorization))return false;
        if(!fresh || mode_==PumpRoomMode::Unknown || mode_==PumpRoomMode::Other ||
           mode!=mode_ || (target_required_ &&
            (!std::isfinite(target_) || !std::isfinite(target_c) || std::fabs(target_c-target_)>0.001)))
            return false;
        pending_=false;return true;
    }
    void clear(){pending_=false;}
private:
    bool pending_=false,target_required_=false;
    uint32_t authorization_=0;
    PumpRoomMode mode_=PumpRoomMode::Unknown;
    double target_=0;
};
} // namespace enevina::cottage

#ifdef ARDUINO
// Read-only atomic authorization epoch, no extra lock or RTOS task.
uint32_t cottage_control_authorization_generation();
#endif
