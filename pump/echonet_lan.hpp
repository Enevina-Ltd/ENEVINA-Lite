#pragma once
// Pure protocol/session policies for the native ECHONET UDP worker.
#include "pump/echonet_lite.hpp"
#include "pump/pump_adapter.hpp"
#include <algorithm>
#include <cstring>
namespace enevina::echonet {

struct Identity {
    std::array<Byte,17> bytes{};
    Byte size=0;
    bool valid() const {
        if(size!=9 && size!=17)return false;
        if((size==9 && bytes[0]!=0xff) || (size==17 && bytes[0]!=0xfe))return false;
        // A manufacturer's fixed all-zero/all-FF unique field is not an identity.
        const auto begin=bytes.begin()+(size==17?4:1),end=bytes.begin()+size;
        return std::any_of(begin,end,[](Byte b){return b!=0;}) &&
               std::any_of(begin,end,[](Byte b){return b!=0xff;});
    }
    bool operator==(const Identity& other) const {
        return valid() && other.valid() && size==other.size &&
            std::equal(bytes.begin(),bytes.begin()+size,other.bytes.begin());
    }
};
inline std::optional<Identity> decode_identity(const Property* p) {
    if(!p || !p->data || (p->size!=9 && p->size!=17))return {};
    Identity id{};id.size=static_cast<Byte>(p->size);
    std::copy_n(p->data,p->size,id.bytes.begin());
    if(!id.valid())return {};
    return id;
}
inline std::optional<Identity> identity(const Frame& req,const Frame& res) {
    if(req.destination!=node_profile || req.service!=Service::Get || !matches(req,res))return {};
    return decode_identity(res.find(0x83));
}
struct Peer {
    uint32_t ipv4=0;Byte instance=0;
    bool valid()const{return ipv4 && instance>=1 && instance<=0x7f;}
    Object object()const{return {0x01,0x30,instance};}
    bool operator==(const Peer& other)const{return ipv4==other.ipv4 && instance==other.instance;}
};
struct CandidateList {
    static constexpr size_t capacity=16;
    std::array<Peer,capacity> items{};
    size_t count=0;
    bool truncated=false;
    bool add(Peer peer) {
        if(!peer.valid())return false;
        for(size_t i=0;i<count;++i)if(items[i]==peer)return true;
        if(count>=capacity){truncated=true;return false;}
        items[count++]=peer;return true;
    }
};
// A reply must be from the expected peer (or a separately checked local peer
// for discovery), received on the same live link before the current deadline.
inline bool response_peer(uint32_t expected,uint32_t actual,uint16_t remote_port,
                          uint32_t elapsed_ms,uint32_t timeout_ms,bool link_current) {
    return link_current && actual && (!expected || expected==actual) &&
        remote_port==port && elapsed_ms<timeout_ms;
}
inline bool same_caps(const Capabilities& a,const Capabilities& b) {
    return a.readable.codes==b.readable.codes && a.writable.codes==b.writable.codes;
}
inline bool same_request(const State& a,const State& b) {
    return a.power==b.power && a.mode==b.mode && a.setpoint_c==b.setpoint_c;
}
inline bool mode_change(const State& s,const Desired& d) {
    return (d.power && *d.power!=s.power) || (d.mode && *d.mode!=s.mode);
}
inline PumpRoomMode room_mode(const State& s) {
    return !s.power?PumpRoomMode::Off:s.mode==Mode::Heat?PumpRoomMode::Heat:PumpRoomMode::Other;
}
struct ModeRange {
    int min_c=0,max_c=0;
    bool enabled=false;
    bool valid()const{return enabled && min_c>=0 && max_c<=50 && min_c<max_c;}
};
struct ApprovedModes {
    ModeRange heat{},cool{};
    bool valid()const{return (heat.enabled||cool.enabled) &&
        (!heat.enabled||heat.valid()) && (!cool.enabled||cool.valid());}
    PumpHeatingLimits heating()const {
        return heat.valid()?PumpHeatingLimits{double(heat.min_c),double(heat.max_c),1,true,true}:PumpHeatingLimits{};
    }
};
inline bool controllable(const Capabilities& cap) {
    for(Byte code:std::array<Byte,3>{0x80,0xb0,0xb3})
        if(!cap.readable.has(code)||!cap.writable.has(code))return false;
    return true;
}
// Never use the heat range for cooling, or allow an unapproved implicit mode.
// A setpoint-only request must apply to a currently running approved mode.
inline std::optional<ModelLimits> approved_limits(const Capabilities& cap,
        const ApprovedModes& approval,const State& current,const Desired& desired) {
    if(!approval.valid() || !controllable(cap) || current.fault==true)return {};
    if(desired.power==false) {
        if(desired.mode || desired.setpoint_c)return {};
        ModelLimits off{};off.power=true;return off;
    }
    const auto mode=desired.mode.value_or(current.mode);
    if(mode!=Mode::Heat && mode!=Mode::Cool)return {};
    if(desired.mode && (desired.power!=true || !desired.setpoint_c))return {};
    if(!desired.mode && (desired.power || !current.power || !desired.setpoint_c))return {};
    const auto range=mode==Mode::Heat?approval.heat:approval.cool;
    if(!range.valid() || !desired.setpoint_c || *desired.setpoint_c<range.min_c ||
       *desired.setpoint_c>range.max_c)return {};
    return ModelLimits{range.min_c,range.max_c,true,mode==Mode::Heat,mode==Mode::Cool};
}
inline bool approved_command_into(Packet& packet,uint16_t tid,Object target,const Capabilities& cap,
        const ApprovedModes& approval,const State& current,const Desired& desired) {
    packet.size=0;
    const auto limits=approved_limits(cap,approval,current,desired);
    return limits && command_into(packet,tid,target,cap,*limits,desired);
}
inline std::optional<Packet> approved_command(uint16_t tid,Object target,const Capabilities& cap,
        const ApprovedModes& approval,const State& current,const Desired& desired) {
    Packet p{};return approved_command_into(p,tid,target,cap,approval,current,desired)?std::optional<Packet>{p}:std::nullopt;
}
inline bool applied(const State& before,const Desired& desired,const State& after) {
    if(!confirmed(desired,after))return false;
    // Target-only commands must not change the active mode/power. OFF may
    // legitimately change/reset the stored mode or setpoint on some devices.
    if(!desired.power && !desired.mode)
        return after.power==before.power && after.mode==before.mode;
    return true;
}
} // namespace enevina::echonet
