#pragma once
// ECHONET Lite format 1: bounded, transport-independent HVAC codec.
// See docs/integrations/ECHONET_LITE.md. No network writes are performed here.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace enevina::echonet {
using Byte = uint8_t;
using Object = std::array<Byte,3>;
inline constexpr Object controller{0x05,0xff,0x01};
inline constexpr Object node_profile{0x0e,0xf0,0x01};
inline constexpr uint16_t port=3610;
inline constexpr const char* multicast="224.0.23.0";
inline constexpr size_t max_packet=1024, max_properties=32;
enum class Service:Byte { SetC=0x61, Get=0x62, SetRes=0x71, GetRes=0x72 };
enum class Mode:Byte { Other=0x40, Auto=0x41, Cool=0x42, Heat=0x43, Dry=0x44, Fan=0x45 };
inline constexpr bool hvac(Object o) {return o[0]==0x01 && o[1]==0x30 && o[2]>=1 && o[2]<=0x7f;}
inline constexpr bool mode_valid(Byte m) {return m>=0x40 && m<=0x45;}
struct Packet {
    std::array<Byte,max_packet> bytes{};
    size_t size=0;
};
struct Property {
    Byte epc=0;
    const Byte* data=nullptr; // borrowed from input packet
    size_t size=0;
};
struct Frame {
    uint16_t tid=0;
    Object source{},destination{};
    Service service=Service::Get;
    std::array<Property,max_properties> properties{};
    size_t count=0;
    const Property* find(Byte epc) const {
        for(size_t i=0;i<count;++i) if(properties[i].epc==epc)return &properties[i];
        return nullptr;
    }
};
inline std::optional<Frame> parse(const Byte* p,size_t n) {
    if(!p || n<14 || n>max_packet || p[0]!=0x10 || p[1]!=0x81 ||
       !p[11] || p[11]>max_properties) return {};
    Frame f{};
    f.tid=uint16_t((uint16_t(p[2])<<8)|p[3]);
    f.source={p[4],p[5],p[6]};
    f.destination={p[7],p[8],p[9]};
    switch(p[10]) {
    case 0x61: case 0x62: case 0x71: case 0x72: f.service=static_cast<Service>(p[10]);break;
    default:return {}; // SNA and other formats are not success responses
    }
    size_t off=12;
    for(size_t i=0;i<p[11];++i) {
        if(n-off<2)return {};
        const Byte epc=p[off++],len=p[off++];
        if(epc<0x80 || n-off<len || f.find(epc))return {};
        if((f.service==Service::Get || f.service==Service::SetRes)?len!=0:len==0)return {};
        f.properties[f.count++]={epc,p+off,len};
        off+=len;
    }
    if(off!=n)return {};
    return f;
}
inline std::optional<Frame> parse(const Packet& p){return parse(p.bytes.data(),p.size);}
inline std::optional<Frame> parse(Packet&&)=delete; // property views must not outlive packet

// Build only explicit Get/SetC requests. Caller owns transport and TID lifecycle.
inline std::optional<Packet> request(uint16_t tid,Object target,Service service,
                                     const Property* props,size_t count) {
    if((!hvac(target) && target!=node_profile) || !props || !count ||
       count>max_properties || (service!=Service::Get && service!=Service::SetC) ||
       (target==node_profile && service!=Service::Get))return {};
    Packet p{};
    const Byte header[]{0x10,0x81,Byte(tid>>8),Byte(tid),controller[0],controller[1],controller[2],
        target[0],target[1],target[2],Byte(service),Byte(count)};
    for(Byte v:header)p.bytes[p.size++]=v;
    for(size_t i=0;i<count;++i) {
        const auto& e=props[i];
        if(e.epc<0x80 || e.size>255 || (e.size && !e.data) ||
           (service==Service::Get?e.size!=0:e.size==0) ||
           max_packet-p.size<2+e.size)return {};
        for(size_t j=0;j<i;++j)if(props[j].epc==e.epc)return {};
        p.bytes[p.size++]=e.epc;p.bytes[p.size++]=Byte(e.size);
        for(size_t j=0;j<e.size;++j)p.bytes[p.size++]=e.data[j];
    }
    return p;
}
inline std::optional<Packet> get(uint16_t tid,Object target,const Byte* codes,size_t count) {
    if(!codes || !count || count>max_properties)return {};
    std::array<Property,max_properties> props{};
    for(size_t i=0;i<count;++i)props[i].epc=codes[i];
    return request(tid,target,Service::Get,props.data(),count);
}
inline std::optional<Packet> discover(uint16_t tid) {
    const Byte code=0xd6;return get(tid,node_profile,&code,1);
}
// Caller must additionally match UDP peer address/port and the current
// outstanding request generation/deadline; TID alone is not freshness.
inline bool matches(const Frame& req,const Frame& res) {
    if(req.source!=controller || req.tid!=res.tid || req.destination!=res.source ||
       req.source!=res.destination || req.count!=res.count)return false;
    if(!((req.service==Service::Get && res.service==Service::GetRes) ||
         (req.service==Service::SetC && res.service==Service::SetRes)))return false;
    for(size_t i=0;i<req.count;++i)if(!res.find(req.properties[i].epc))return false;
    return true;
}
struct Instances {
    std::array<Object,84> objects{};
    size_t count=0;
};
inline std::optional<Instances> discovery_result(const Frame& req,const Frame& res) {
    if(req.destination!=node_profile || req.service!=Service::Get || !matches(req,res))return {};
    const auto* p=res.find(0xd6);
    if(!p || !p->size || p->data[0]>84 || p->size!=1+3*size_t(p->data[0]))return {};
    Instances out{};
    for(size_t i=0;i<p->data[0];++i) {
        const Object o{p->data[1+3*i],p->data[2+3*i],p->data[3+3*i]};
        if(!hvac(o))continue;
        for(size_t j=0;j<out.count;++j)if(out.objects[j]==o)return {};
        out.objects[out.count++]=o;
    }
    return out;
}
struct PropertyMap {
    std::array<bool,128> codes{};
    bool has(Byte epc)const{return epc>=0x80 && codes[epc-0x80];}
};
inline std::optional<PropertyMap> property_map(const Property& p) {
    if((p.epc!=0x9d && p.epc!=0x9e && p.epc!=0x9f) ||
       !p.data || !p.size || p.data[0]>128)return {};
    const size_t count=p.data[0];PropertyMap map{};
    if(count<16) {
        if(p.size!=count+1)return {};
        for(size_t i=1;i<p.size;++i) {
            const auto v=p.data[i];
            if(v<0x80 || map.has(v))return {};
            map.codes[v-0x80]=true;
        }
    } else {
        if(p.size!=17)return {};
        size_t actual=0;
        for(size_t low=0;low<16;++low)for(size_t bit=0;bit<8;++bit)
            if(p.data[1+low]&(1u<<bit)) {
                map.codes[bit*16+low]=true;++actual;
            }
        if(actual!=count)return {};
    }
    return map;
}
struct Capabilities {PropertyMap readable{},writable{};};
inline std::optional<Capabilities> capabilities(const Frame& req,const Frame& res) {
    if(!hvac(req.destination) || req.service!=Service::Get || !matches(req,res))return {};
    const auto* r=res.find(0x9f);const auto* w=res.find(0x9e);
    if(!r || !w)return {};
    auto rm=property_map(*r),wm=property_map(*w);
    if(!rm || !wm || !rm->has(0x80) || !rm->has(0xb0))return {};
    return Capabilities{*rm,*wm};
}
inline std::optional<Packet> read_capabilities(uint16_t tid,Object target) {
    if(!hvac(target))return {};
    const Byte codes[]{0x9e,0x9f};return get(tid,target,codes,2);
}
inline std::optional<Packet> read_state(uint16_t tid,Object target,const Capabilities& cap) {
    if(!hvac(target) || !cap.readable.has(0x80) || !cap.readable.has(0xb0))return {};
    Byte codes[9];size_t count=0;
    for(Byte code:std::array<Byte,9>{0x80,0xb0,0xb3,0xbb,0xbe,0xba,0xa0,0x88,0xaa})
        if(cap.readable.has(code))codes[count++]=code;
    return get(tid,target,codes,count);
}
struct State {
    bool power=false;
    Mode mode=Mode::Other;
    std::optional<int> setpoint_c,indoor_c,outdoor_c,humidity_pct;
    std::optional<Byte> fan,special_state;
    std::optional<bool> fault;
};
inline std::optional<int> temperature(const Property* p) {
    if(!p || p->size!=1)return {};
    const int raw=p->data[0],v=raw<128?raw:raw-256;
    if(v< -127 || v>125)return {};
    return v;
}
inline std::optional<State> state(const Frame& req,const Frame& res) {
    if(!hvac(req.destination) || req.service!=Service::Get || !matches(req,res))return {};
    const auto* power=res.find(0x80);const auto* mode=res.find(0xb0);
    if(!power || power->size!=1 || (power->data[0]!=0x30 && power->data[0]!=0x31) ||
       !mode || mode->size!=1 || !mode_valid(mode->data[0]))return {};
    State s{};s.power=power->data[0]==0x30;s.mode=static_cast<Mode>(mode->data[0]);
    if(auto p=res.find(0xb3);p && p->size==1 && p->data[0]<=50)s.setpoint_c=p->data[0];
    s.indoor_c=temperature(res.find(0xbb));s.outdoor_c=temperature(res.find(0xbe));
    if(auto p=res.find(0xba);p && p->size==1 && p->data[0]<=100)s.humidity_pct=p->data[0];
    if(auto p=res.find(0xa0);p && p->size==1 &&
       (p->data[0]==0x41 || (p->data[0]>=0x31 && p->data[0]<=0x38)))s.fan=p->data[0];
    if(auto p=res.find(0x88);p && p->size==1 && (p->data[0]==0x41 || p->data[0]==0x42))
        s.fault=p->data[0]==0x41;
    if(auto p=res.find(0xaa);p && p->size==1)s.special_state=p->data[0];
    return s;
}
// Property maps do not prove which values a given model accepts.
// The caller must supply independently confirmed per-model limits and modes.
struct ModelLimits {
    int min_c=0,max_c=0;
    bool power=false,heat=false,cool=false,auto_mode=false,dry=false,fan=false;
    bool valid()const{return min_c>=0 && max_c<=50 && min_c<max_c;}
    bool permits(Mode m)const {
        switch(m){case Mode::Heat:return heat;case Mode::Cool:return cool;case Mode::Auto:return auto_mode;
        case Mode::Dry:return dry;case Mode::Fan:return fan;default:return false;}
    }
};
struct Desired {std::optional<bool> power;std::optional<Mode> mode;std::optional<int> setpoint_c;};
// Produces wire bytes only; not authorization to send. SetRes means accepted,
// not applied. The firmware must read back every requested field before ACK.
inline std::optional<Packet> command(uint16_t tid,Object target,const Capabilities& cap,
                                     const ModelLimits& limits,const Desired& d) {
    if(!hvac(target))return {};
    std::array<Property,3> props{};std::array<Byte,3> values{};size_t count=0;
    const auto append=[&](Byte epc,Byte value){
        if(!cap.writable.has(epc) || !cap.readable.has(epc))return false;
        values[count]=value;props[count]={epc,&values[count],1};++count;return true;
    };
    if(d.mode && (!limits.permits(*d.mode) || !append(0xb0,Byte(*d.mode))))return {};
    if(d.setpoint_c && (!limits.valid() || *d.setpoint_c<limits.min_c ||
       *d.setpoint_c>limits.max_c || !append(0xb3,Byte(*d.setpoint_c))))return {};
    if(d.power && (!limits.power || !append(0x80,*d.power?0x30:0x31)))return {};
    return request(tid,target,Service::SetC,props.data(),count);
}
inline bool confirmed(const Desired& d,const State& s) {
    return (d.power || d.mode || d.setpoint_c) &&
        (!d.power || *d.power==s.power) &&
        (!d.mode || *d.mode==s.mode) &&
        (!d.setpoint_c || (s.setpoint_c && *d.setpoint_c==*s.setpoint_c));
}
} // namespace enevina::echonet
