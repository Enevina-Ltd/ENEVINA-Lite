#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "pump/pump_adapter.hpp"
#include "cottage/mitsubishi_control_policy.hpp"

namespace enevina::cottage {
// The standard ESPHome climate GET reports a REQUESTED operating state,
// not a proven new UART sample or compressor operating state. This tracker
// models the control input of the gateway; physical response is learned
// independently from measured room temperature (and optional pump meter).
// Neither an SSE receipt nor a gateway GET manufactures bus provenance.
class CN105GatewayRequestHistory {
public:
    static constexpr uint64_t max_gap_ms=180000;
    void invalidate() {
        transition_valid_=false;
        if(valid_) {valid_=false;++generation_;}
    }
    bool note(bool heat,double target,int64_t utc,uint64_t ms,
              uint32_t endpoint,uint32_t link) {
        if(utc<=0 || !mitsubishi_heating_limits.accepts(target)) {
            invalidate();return false;
        }
        const bool continuous=valid_ && endpoint==endpoint_ && link==link_ &&
            utc>=received_utc_ && utc-received_utc_<=180 &&
            ms>=received_ms_ && ms-received_ms_<=max_gap_ms;
        const bool mode_changed=!continuous || heat_!=heat;
        const bool request_changed=mode_changed ||
            (heat && std::fabs(target-target_)>0.001);
        if(request_changed) {
            // Keep only a continuously bracketed single mode change; a gap,
            // reset or setpoint-only change cannot manufacture a transition.
            transition_valid_=continuous && heat_!=heat;
            transition_before_utc_=received_utc_;
            transition_after_utc_=utc;
            transition_generation_=generation_+1;
        }
        if(mode_changed)mode_since_ms_=ms;
        if(request_changed) {
            request_since_ms_=ms;
            ++generation_;
        }
        valid_=true;heat_=heat;target_=target;received_utc_=utc;received_ms_=ms;
        endpoint_=endpoint;link_=link;
        return true;
    }
    PumpRoomState snapshot(int64_t utc,uint64_t ms,uint32_t endpoint,uint32_t link) const {
        PumpRoomState s{};
        if(!valid_ || endpoint!=endpoint_ || link!=link_ ||
           utc<received_utc_ || utc-received_utc_>180 ||
           ms<received_ms_ || ms-received_ms_>max_gap_ms)return s;
        s.type=HeatPumpType::AirToAir;
        s.protocol=PumpRoomProtocol::Cn105EspHome;
        s.source=PumpStateSource::Gateway;
        s.mode=heat_?PumpRoomMode::Heat:PumpRoomMode::Off;
        s.setpoint_c=target_;
        s.heating=mitsubishi_heating_limits;
        s.request_generation=generation_;
        s.endpoint_generation=endpoint_;
        s.link_generation=link_;
        // Do not extrapolate unobserved time after the last successful GET.
        s.mode_minutes=static_cast<unsigned>(std::min<uint64_t>((received_ms_-mode_since_ms_)/60000,600));
        s.request_minutes=static_cast<unsigned>(std::min<uint64_t>((received_ms_-request_since_ms_)/60000,600));
        s.evidence={received_utc_,0,false};
        return s;
    }
    // Approximate the transition time by the midpoint of its observed GET
    // bracket. Multiple changes, a communication gap, changed identity or
    // invalid endpoints are not eligible. Fraction is relative to the actual
    // two boundary observations, not a claim of UART timing.
    bool single_mode_transition(const PumpRoomState& begin,const PumpRoomState& end,
                                double& fraction_before) const {
        if(!valid_ || !transition_valid_ ||
           begin.source!=PumpStateSource::Gateway || end.source!=PumpStateSource::Gateway ||
           begin.protocol!=PumpRoomProtocol::Cn105EspHome || end.protocol!=PumpRoomProtocol::Cn105EspHome ||
           begin.endpoint_generation!=end.endpoint_generation || end.endpoint_generation!=endpoint_ ||
           begin.link_generation!=end.link_generation || end.link_generation!=link_ ||
           begin.request_generation+1!=end.request_generation ||
           end.request_generation!=transition_generation_ || end.request_generation!=generation_ ||
           begin.mode==end.mode ||
           (begin.mode!=PumpRoomMode::Heat && begin.mode!=PumpRoomMode::Off) ||
           (end.mode!=PumpRoomMode::Heat && end.mode!=PumpRoomMode::Off) ||
           end.mode!=(heat_?PumpRoomMode::Heat:PumpRoomMode::Off) ||
           end.evidence.received_utc!=received_utc_)return false;
        const int64_t start=begin.evidence.received_utc,finish=end.evidence.received_utc;
        if(start<=0 || finish-start<840 || finish-start>960 ||
           transition_before_utc_<start || transition_after_utc_>finish ||
           transition_after_utc_<=transition_before_utc_)return false;
        fraction_before=(double(transition_before_utc_-start)+
            0.5*double(transition_after_utc_-transition_before_utc_))/double(finish-start);
        return fraction_before>0 && fraction_before<1;
    }
private:
    bool transition_valid_=false;
    uint32_t transition_generation_=0;
    int64_t transition_before_utc_=0,transition_after_utc_=0;
    bool valid_=false,heat_=false;
    double target_=0;
    uint32_t generation_=0,endpoint_=0,link_=0;
    int64_t received_utc_=0;
    uint64_t received_ms_=0,mode_since_ms_=0,request_since_ms_=0;
};

// Require both ends to be observed on the same uninterrupted request.
// The model learns the gateway request, not a claim of compressor uptime.
inline bool same_cn105_gateway_request(const PumpRoomState& begin,const PumpRoomState& end) {
    return begin.source==PumpStateSource::Gateway && end.source==PumpStateSource::Gateway &&
        begin.protocol==PumpRoomProtocol::Cn105EspHome && end.protocol==PumpRoomProtocol::Cn105EspHome &&
        begin.request_generation==end.request_generation &&
        begin.endpoint_generation==end.endpoint_generation &&
        begin.link_generation==end.link_generation &&
        begin.mode==end.mode &&
        (end.mode==PumpRoomMode::Off ||
         (end.mode==PumpRoomMode::Heat && std::fabs(begin.setpoint_c-end.setpoint_c)<=0.001));
}
} // namespace enevina::cottage
