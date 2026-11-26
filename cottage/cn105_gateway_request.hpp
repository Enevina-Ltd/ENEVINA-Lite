#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "pump/pump_adapter.hpp"
#include "cottage/mitsubishi_control_policy.hpp"

namespace enevina::cottage {
// The standard ESPHome climate GET reports a REQUESTED operating state,
// not a proven new UART sample or compressor operating state. This tracker
// models the control input of the gateway; physical response is learned
// independently from measured room temperature (and optional pump meter).
// Neither an SSE receipt nor a gateway GET manufactures bus provenance.
// Deltas from the start of one learning interval, plus the actual local
// command dispatch and standard gateway GET acknowledgement. No UART proof.
struct CN105SlotCommands {
    uint32_t sent=0,confirmed=0,failed=0;
    ControlStep step=ControlStep::None;
    int64_t sent_utc=0,ack_utc=0;
    double target_c=0;
};
struct CN105LearningRequest {
    bool valid=false;
    double before_fraction=-1.0; // unchanged request if -1, otherwise one switch
};
// Only recognized ESPHome climate modes. An unknown/missing response is
// neither OFF nor evidence of another powered mode.
inline PumpRoomMode cn105_gateway_mode(const char* mode) {
    if(!mode)return PumpRoomMode::Unknown;
    if(!std::strcmp(mode,"HEAT"))return PumpRoomMode::Heat;
    if(!std::strcmp(mode,"OFF"))return PumpRoomMode::Off;
    for(const char* other: {"COOL","DRY","AUTO","HEAT_COOL","FAN_ONLY"})
        if(!std::strcmp(mode,other))return PumpRoomMode::Other;
    return PumpRoomMode::Unknown;
}
class CN105GatewayRequestHistory {
public:
    static constexpr uint64_t max_gap_ms=180000;
    void invalidate() {
        transition_valid_=false;
        if(valid_) {valid_=false;++generation_;}
    }
    bool note(bool heat,double target,int64_t utc,uint64_t ms,
              uint32_t endpoint,uint32_t link) {
        return note_mode(heat?PumpRoomMode::Heat:PumpRoomMode::Off,target,utc,ms,endpoint,link);
    }
    bool note_mode(PumpRoomMode mode,double target,int64_t utc,uint64_t ms,
                   uint32_t endpoint,uint32_t link) {
        const bool heat=mode==PumpRoomMode::Heat;
        if(utc<=0 || mode==PumpRoomMode::Unknown ||
           (heat?!mitsubishi_heating_limits.accepts(target):
                 (!std::isfinite(target) || (target!=0 && (target<5 || target>35))))) {
            invalidate();return false;
        }
        const bool continuous=valid_ && endpoint==endpoint_ && link==link_ &&
            utc>=received_utc_ && utc-received_utc_<=180 &&
            ms>=received_ms_ && ms-received_ms_<=max_gap_ms;
        const bool mode_changed=!continuous || mode_!=mode;
        const bool request_changed=mode_changed ||
            (heat && std::fabs(target-target_)>0.001);
        if(request_changed) {
            // Keep only a continuously bracketed single mode change; a gap,
            // reset or setpoint-only change cannot manufacture a transition.
            transition_valid_=continuous && mode_!=PumpRoomMode::Other &&
                mode!=PumpRoomMode::Other && mode_!=mode;
            transition_before_utc_=received_utc_;
            transition_after_utc_=utc;
            transition_before_ms_=received_ms_;
            transition_after_ms_=ms;
            transition_generation_=generation_+1;
        }
        if(mode_changed)mode_since_ms_=ms;
        if(request_changed) {
            request_since_ms_=ms;
            ++generation_;
        }
        valid_=true;mode_=mode;target_=target;received_utc_=utc;received_ms_=ms;
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
        s.mode=mode_;
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
    bool matches_current(const PumpRoomState& end) const {
        return valid_ && end.source==PumpStateSource::Gateway &&
            end.protocol==PumpRoomProtocol::Cn105EspHome &&
            end.endpoint_generation==endpoint_ && end.link_generation==link_ &&
            end.request_generation==generation_ && end.evidence.received_utc==received_utc_ &&
            end.mode==mode_ &&
            std::isfinite(end.setpoint_c) && std::fabs(end.setpoint_c-target_)<=0.001;
    }
    // Approximate the transition time by the midpoint of its observed GET
    // bracket. Multiple changes, a communication gap, changed identity or
    // invalid endpoints are not eligible. Fraction is relative to the actual
    // two boundary observations, not a claim of UART timing.
    bool single_mode_transition(const PumpRoomState& begin,const PumpRoomState& end,
                                double& fraction_before) const {
        if(!matches_current(end) || !transition_valid_ ||
           begin.source!=PumpStateSource::Gateway || end.source!=PumpStateSource::Gateway ||
           begin.protocol!=PumpRoomProtocol::Cn105EspHome || end.protocol!=PumpRoomProtocol::Cn105EspHome ||
           begin.endpoint_generation!=end.endpoint_generation || end.endpoint_generation!=endpoint_ ||
           begin.link_generation!=end.link_generation || end.link_generation!=link_ ||
           begin.request_generation+1!=end.request_generation ||
           end.request_generation!=transition_generation_ || end.request_generation!=generation_ ||
           begin.mode==end.mode ||
           (begin.mode!=PumpRoomMode::Heat && begin.mode!=PumpRoomMode::Off) ||
           (end.mode!=PumpRoomMode::Heat && end.mode!=PumpRoomMode::Off) ||
           end.mode!=mode_ ||
           end.evidence.received_utc!=received_utc_)return false;
        const int64_t start=begin.evidence.received_utc,finish=end.evidence.received_utc;
        if(start<=0 || finish-start<840 || finish-start>960 ||
           transition_before_utc_<start || transition_after_utc_>finish ||
           transition_after_utc_<transition_before_utc_ ||
           transition_after_ms_<=transition_before_ms_)return false;
        // Rapid local GET acknowledgement may land in the same UTC second.
        // A strictly ordered monotonic bracket proves distinct observations;
        // the second-resolution midpoint is accurate to within one second.
        fraction_before=(double(transition_before_utc_-start)+
            0.5*double(transition_after_utc_-transition_before_utc_))/double(finish-start);
        return fraction_before>0 && fraction_before<1;
    }
    // A Lite-originated transition is just as useful as an external one,
    // but only if exactly one mode POST was dispatched and acknowledged,
    // and continuous GET history independently brackets that very change.
    // A request with uncertain outcome or extra setpoint/ mode commands
    // must not be treated as observed heating input.
    bool confirmed_mode_transition(const PumpRoomState& begin,const PumpRoomState& end,
            const CN105SlotCommands& cmd,double& fraction_before) const {
        if(cmd.sent!=1 || cmd.confirmed!=1 || cmd.failed!=0)return false;
        if(end.mode==PumpRoomMode::Heat) {
            if(cmd.step!=ControlStep::SetHeat || !std::isfinite(cmd.target_c) ||
               std::fabs(cmd.target_c-end.setpoint_c)>0.001)return false;
        } else if(end.mode==PumpRoomMode::Off) {
            if(cmd.step!=ControlStep::SetOff)return false;
        } else return false;
        double observed=0;
        if(!single_mode_transition(begin,end,observed))return false;
        const int64_t start=begin.evidence.received_utc,finish=end.evidence.received_utc;
        if(cmd.sent_utc<start || cmd.sent_utc>transition_after_utc_ ||
           cmd.ack_utc<transition_after_utc_ || cmd.ack_utc>finish ||
           cmd.ack_utc<cmd.sent_utc)return false;
        // A GET of the old request may follow the POST while the bridge is
        // processing it. The actual change cannot be inferred before either
        // that observation or dispatch. Still use an estimated midpoint, not
        // a fabricated physical compressor timestamp.
        const int64_t lower=std::max(transition_before_utc_,cmd.sent_utc);
        fraction_before=(double(lower-start)+0.5*double(transition_after_utc_-lower))/double(finish-start);
        return fraction_before>0 && fraction_before<1;
    }
private:
    bool transition_valid_=false;
    uint32_t transition_generation_=0;
    int64_t transition_before_utc_=0,transition_after_utc_=0;
    uint64_t transition_before_ms_=0,transition_after_ms_=0;
    bool valid_=false;
    PumpRoomMode mode_=PumpRoomMode::Unknown;
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
// Single shared eligibility rule for host tests and production runtime.
inline CN105LearningRequest cn105_learning_request(const CN105GatewayRequestHistory& history,
        const PumpRoomState& begin,const PumpRoomState& end,const CN105SlotCommands& commands) {
    if(begin.command_unconfirmed || end.command_unconfirmed ||
       begin.evidence.received_utc<=0 || !history.matches_current(end))return {};
    const int64_t span=end.evidence.received_utc-begin.evidence.received_utc;
    if(span<840 || span>960)return {};
    const bool quiet=commands.sent==0 && commands.confirmed==0 && commands.failed==0;
    if(quiet && end.request_minutes>=14 && same_cn105_gateway_request(begin,end))return {true,-1.0};
    double fraction=0;
    const bool transition=quiet?history.single_mode_transition(begin,end,fraction):
        history.confirmed_mode_transition(begin,end,commands,fraction);
    return transition?CN105LearningRequest{true,fraction}:CN105LearningRequest{};
}
} // namespace enevina::cottage
