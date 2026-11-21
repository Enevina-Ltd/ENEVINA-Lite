#pragma once
#include <array>
#include <cstdint>
namespace enevina::midea {
struct ReadIdentity {
    uint64_t device_id=0;
    uint32_t address=0,link_generation=0,endpoint_generation=0,configuration_generation=0;
    uint8_t transport=0;
};
inline bool same_read_identity(const ReadIdentity& a,const ReadIdentity& b) {
    return a.device_id==b.device_id && a.address==b.address &&
        a.link_generation==b.link_generation &&
        a.configuration_generation==b.configuration_generation &&
        a.endpoint_generation==b.endpoint_generation && a.transport==b.transport;
}
class ReadPlan {
public:
    // Identity change invalidates every negotiation result; the caller also
    // discards its associated profile. No state sample is stored here.
    bool bind(const ReadIdentity& identity) {
        if(bound_ && same_read_identity(identity_,identity))return false;
        identity_=identity;bound_=true;attempts_={};complete_=0;cursor_=0;
        return true;
    }
    int next_probe() const {
        for(uint8_t offset=0;offset<3;++offset) {
            const uint8_t slot=(cursor_+offset)%3;
            if(!(complete_&(1u<<slot)) && attempts_[slot]<2)return slot;
        }
        return -1;
    }
    // Appliance metadata, primary B5 and additional B5 each get at most two
    // read-only attempts. A timeout is never an unsupported capability flag.
    void finish_probe(uint8_t slot,bool success) {
        if(slot>=3)return;
        if(attempts_[slot]<2)++attempts_[slot];
        if(success)complete_|=static_cast<uint8_t>(1u<<slot);
        cursor_=(slot+1)%3;
    }
private:
    ReadIdentity identity_{};
    std::array<uint8_t,3> attempts_{};
    uint8_t complete_=0,cursor_=0;
    bool bound_=false;
};
inline uint32_t exchange_time_left(uint32_t started,uint32_t budget,uint32_t now) {
    const uint32_t elapsed=now-started;
    return elapsed<budget ? budget-elapsed : 0;
}
inline bool command_window_open(int64_t now,int64_t deadline,uint32_t started,
                                 uint32_t now_ms,uint32_t budget) {
    return now>=1700000000 && deadline>now &&
        exchange_time_left(started,budget,now_ms)>0;
}
}
