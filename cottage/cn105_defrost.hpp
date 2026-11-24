#pragma once
#include <cstdint>
#include <cstring>

namespace enevina::cottage {
// Optional standard ESPHome observations, NOT verified UART measurements.
// A positive report can conservatively defer a normal automatic write.
// Missing/unknown data never proves that defrost is over.
enum class CN105DefrostSignal : uint8_t { Unknown, Other, Defrost };
enum class CN105DefrostSource : uint8_t { SubMode, ClimateAction };
enum class CN105DefrostState : uint8_t { Unknown, ReportedOther, ReportedDefrost, SignalLost, Recovery, Overdue };

inline CN105DefrostSignal cn105_submode_signal(const char* s) {
    if(!s)return CN105DefrostSignal::Unknown;
    if(!std::strcmp(s,"DEFROST"))return CN105DefrostSignal::Defrost;
    if(!std::strcmp(s,"NORMAL") || !std::strcmp(s,"PREHEAT") ||
       !std::strcmp(s,"STANDBY"))return CN105DefrostSignal::Other;
    return CN105DefrostSignal::Unknown;
}
inline CN105DefrostSignal cn105_submode_report(const char* value,const char* state) {
    const auto a=cn105_submode_signal(value),b=cn105_submode_signal(state);
    // A contradictory or missing field never clears an active report.
    if(a==CN105DefrostSignal::Defrost || b==CN105DefrostSignal::Defrost)
        return CN105DefrostSignal::Defrost;
    if(!value)return b;
    if(!state)return a;
    return a==b?a:CN105DefrostSignal::Unknown;
}
inline CN105DefrostSignal cn105_action_signal(const char* s) {
    if(!s)return CN105DefrostSignal::Unknown;
    if(!std::strcmp(s,"DEFROSTING") || !std::strcmp(s,"defrosting") ||
       !std::strcmp(s,"DEFROST"))return CN105DefrostSignal::Defrost;
    if(!std::strcmp(s,"HEATING") || !std::strcmp(s,"heating") ||
       !std::strcmp(s,"IDLE") || !std::strcmp(s,"idle") ||
       !std::strcmp(s,"OFF") || !std::strcmp(s,"off"))return CN105DefrostSignal::Other;
    return CN105DefrostSignal::Unknown;
}
inline const char* cn105_defrost_state_name(CN105DefrostState s) {
    switch(s) {
    case CN105DefrostState::ReportedOther:return "REPORTED_OTHER";
    case CN105DefrostState::ReportedDefrost:return "REPORTED_DEFROST";
    case CN105DefrostState::SignalLost:return "DEFROST_SIGNAL_LOST";
    case CN105DefrostState::Recovery:return "RECOVERY";
    case CN105DefrostState::Overdue:return "DEFROST_REPORT_OVERDUE";
    case CN105DefrostState::Unknown:break;
    }
    return "UNKNOWN";
}
struct CN105DefrostView {
    CN105DefrostState state=CN105DefrostState::Unknown;
    bool defer_write=false;
    bool ever_reported_defrost=false;
    uint32_t reports=0; // boot-local positive observations, not episode count
    uint32_t generation=0; // new positive episodes, including each source
    uint32_t submode_starts=0,submode_ends=0,action_starts=0,action_ends=0;
    uint32_t submode_last_duration_s=0,action_last_duration_s=0;
};
// Caller serializes access with the existing data lock. Keep sources
// independent: a generic HEATING action must not clear a Sub Mode DEFROST.
// The bounded hold starts at the first positive report (FD documented
// 10-min interruption + 2-min engineering margin). Replayed cached values
// cannot extend it forever. Overdue is UNKNOWN physical state, not a
// confirmed completion. No shorter dwell is permitted based on this data.
class CN105DefrostHistory {
public:
    static constexpr uint64_t recent_ms=180000, lost_grace_ms=720000, recovery_ms=60000;
    void note(CN105DefrostSource source,CN105DefrostSignal signal,uint64_t ms,uint32_t endpoint) {
        if(!bound_ || endpoint!=endpoint_) {
            channels_[0]={};channels_[1]={};endpoint_=endpoint;bound_=true;
        }
        auto& c=channels_[static_cast<unsigned>(source)];
        if(c.seen && ms<c.observed_ms)return;
        if(signal==CN105DefrostSignal::Unknown) {
            // Missing data invalidates an ordinary report, but must not
            // clear or refresh a previous positive report.
            if(c.signal!=CN105DefrostSignal::Defrost) {
                c.seen=true;c.signal=signal;c.observed_ms=ms;
            }
            return;
        }
        if(signal==CN105DefrostSignal::Defrost) {
            ++reports_;
            if(c.signal!=CN105DefrostSignal::Defrost) {
                ++generation_;++c.starts;c.first_positive_ms=ms;
            }
            c.positive_seen=true;
            c.recover_until=0;
        } else if(c.signal==CN105DefrostSignal::Defrost) {
            // Count only a bracketed reported end, not a late stale clear.
            if(ms>=c.first_positive_ms && ms-c.first_positive_ms<=lost_grace_ms) {
                ++c.ends;
                c.last_duration_s=static_cast<uint32_t>((ms-c.first_positive_ms)/1000);
            }
            c.recover_until=ms+recovery_ms;
        }
        c.seen=true;c.signal=signal;c.observed_ms=ms;
    }
    CN105DefrostView view(uint64_t ms,uint32_t endpoint)const {
        CN105DefrostView v{};
        v.reports=reports_;v.generation=generation_;
        if(!bound_ || endpoint!=endpoint_)return v;
        v.submode_starts=channels_[0].starts;v.submode_ends=channels_[0].ends;
        v.action_starts=channels_[1].starts;v.action_ends=channels_[1].ends;
        v.submode_last_duration_s=channels_[0].last_duration_s;
        v.action_last_duration_s=channels_[1].last_duration_s;
        bool active=false,lost=false,recovering=false,other=false,overdue=false;
        for(unsigned i=0;i<2;++i) {
            const auto& c=channels_[i];
            v.ever_reported_defrost|=c.positive_seen;
            if(!c.seen || ms<c.observed_ms)continue;
            const uint64_t age=ms-c.observed_ms;
            if(c.signal==CN105DefrostSignal::Defrost) {
                if(ms<c.first_positive_ms || ms-c.first_positive_ms>lost_grace_ms)
                    overdue=true;
                else if(age<=recent_ms)active=true;
                else lost=true;
            } else {
                recovering|=ms<c.recover_until;
                // Generic action is not a capability declaration. A
                // Sub Mode non-empty value is only a reported other state.
                other|=c.signal==CN105DefrostSignal::Other &&
                    age<=recent_ms && (i==0 || c.positive_seen);
            }
        }
        v.state=active?CN105DefrostState::ReportedDefrost:
            lost?CN105DefrostState::SignalLost:recovering?CN105DefrostState::Recovery:
            overdue?CN105DefrostState::Overdue:other?CN105DefrostState::ReportedOther:CN105DefrostState::Unknown;
        v.defer_write=active||lost||recovering;
        return v;
    }
private:
    struct Channel {
        CN105DefrostSignal signal=CN105DefrostSignal::Unknown;
        bool seen=false,positive_seen=false;
        uint64_t observed_ms=0,recover_until=0,first_positive_ms=0;
        uint32_t starts=0,ends=0,last_duration_s=0;
    };
    Channel channels_[2]{};
    uint32_t endpoint_=0,reports_=0,generation_=0;
    bool bound_=false;
};
inline bool cn105_defrost_defer_write(const CN105DefrostView& view,bool heating) {
    // OFF is never turned into a new heating request by the defrost guard.
    return heating && view.defer_write;
}
} // namespace enevina::cottage
