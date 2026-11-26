#pragma once
#include "pump/midea_discovery_info.hpp"
#include <array>
#include <cstdint>

namespace enevina::midea {
using DiscoveryIp=std::array<uint8_t,4>;
inline constexpr uint32_t discovery_queue_ms=5500,discovery_receive_ms=1350,
    discovery_result_ms=30000,discovery_poll_ms=25,discovery_max_poll_gap_ms=1000;
inline constexpr size_t discovery_receive_quota=4,discovery_packet_max=512;
struct DiscoveryLink {
    uint32_t wifi=0,endpoint=0;
    DiscoveryIp ip{},mask{};
    bool operator==(const DiscoveryLink& o)const {
        return wifi==o.wifi && endpoint==o.endpoint && ip==o.ip && mask==o.mask;
    }
};
enum class DiscoveryPhase:uint8_t { Idle,Queued,Receiving,Done,Failed };
struct DiscoveryHit {
    DiscoveryIp ip{};
    uint64_t id=0;
    uint8_t protocol=0;
    bool metadata_known=false;
    DiscoveryInfo info{};
    bool conflicted=false; // same identity claimed by two LAN addresses
};
// Owned exclusively by the Arduino loop task. Time comparisons are wrap safe.
// A scan is a read-only shortlist; its identity never authorizes a write.
class DiscoveryJob {
public:
    uint32_t start(uint32_t now,const DiscoveryLink& link) {
        if(active() && link==link_)return id_;
        if(++id_==0)++id_;
        phase_=DiscoveryPhase::Queued;link_=link;queued_=now;
        sent_=finished_=last_receive_=0;count_=0;truncated_=false;hits_={};error_="";
        return id_;
    }
    bool active()const{return phase_==DiscoveryPhase::Queued||phase_==DiscoveryPhase::Receiving;}
    bool current(const DiscoveryLink& link)const{return id_!=0 && link_==link;}
    bool queue_expired(uint32_t now)const {
        return phase_==DiscoveryPhase::Queued && now-queued_>=discovery_queue_ms;
    }
    bool receive_expired(uint32_t now)const {
        return phase_==DiscoveryPhase::Receiving && now-sent_>=discovery_receive_ms;
    }
    bool available(uint32_t now,uint32_t id)const {
        return id && id==id_ && (active() || (phase_!=DiscoveryPhase::Idle && now-finished_<discovery_result_ms));
    }
    void receiving(uint32_t now) {
        if(phase_==DiscoveryPhase::Queued){phase_=DiscoveryPhase::Receiving;sent_=last_receive_=now;}
    }
    bool receive_interrupted(uint32_t now)const {
        return phase_==DiscoveryPhase::Receiving && now-last_receive_>discovery_max_poll_gap_ms;
    }
    void polled(uint32_t now){last_receive_=now;}
    void finish(uint32_t now,const char* error=nullptr) {
        if(!active())return;
        phase_=error?DiscoveryPhase::Failed:DiscoveryPhase::Done;
        finished_=now;error_=error?error:"";
        if(error){count_=0;truncated_=false;hits_={};}
    }
    void accept(const DiscoveryIp& src,const uint8_t* packet,size_t bytes) {
        if(phase_!=DiscoveryPhase::Receiving || src==link_.ip || !bytes || bytes>discovery_packet_max)return;
        for(size_t i=0;i<4;++i)if((src[i]&link_.mask[i])!=(link_.ip[i]&link_.mask[i]))return;
        DiscoveredDevice candidate{};
        if(!parse_discovery_reply(packet,bytes,candidate))return;
        DiscoveryInfo info{};
        const bool known=parse_discovery_info(packet,bytes,info);
        for(size_t i=0;i<count_;++i) {
            auto& hit=hits_[i];
            if(hit.id!=candidate.id||hit.protocol!=candidate.protocol)continue;
            // A later, complete reply enriches only the same source.
            if(hit.ip!=src)hit.conflicted=true;
            if(known&&hit.ip==src){hit.info=info;hit.metadata_known=true;}
            return;
        }
        if(count_==hits_.size()){truncated_=true;return;}
        hits_[count_++]={src,candidate.id,candidate.protocol,known,info};
    }
    uint32_t id()const{return id_;}
    DiscoveryPhase phase()const{return phase_;}
    const DiscoveryLink& link()const{return link_;}
    const char* error()const{return error_;}
    size_t count()const{return count_;}
    bool truncated()const{return truncated_;}
    const DiscoveryHit& hit(size_t i)const{return hits_[i];}
private:
    DiscoveryPhase phase_=DiscoveryPhase::Idle;
    DiscoveryLink link_{};
    uint32_t id_=0,queued_=0,sent_=0,finished_=0,last_receive_=0;
    size_t count_=0;
    bool truncated_=false;
    const char* error_="";
    std::array<DiscoveryHit,8> hits_{};
};
}
