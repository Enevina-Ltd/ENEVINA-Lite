#include "pump/haier_uart.hpp"
#include <algorithm>
#include <cmath>

namespace enevina::haier {
namespace {
constexpr uint8_t separator=0xff, escape=0x55, crc_flag=0x40;
constexpr size_t header_size=8;
constexpr size_t hon_control_size=10, hon_status_size=34, smart_control_size=24;

uint8_t checksum(const uint8_t* bytes,size_t size) {
    uint8_t sum=0;
    for(size_t i=0;i<size;++i) {
        sum+=bytes[i];
        if(bytes[i]==separator)sum+=escape;
    }
    return sum;
}
uint16_t crc16(const uint8_t* bytes,size_t size) {
    uint16_t value=0;
    for(size_t i=0;i<size;++i) {
        value^=bytes[i];
        for(unsigned bit=0;bit<8;++bit)
            value=(value>>1)^((value&1)?0xa001:0);
    }
    return value;
}
Mode operating_mode(Family family,uint8_t value) {
    constexpr Mode hon[]{Mode::Auto,Mode::Cool,Mode::Dry,Mode::Dry,Mode::Heat,Mode::Other,Mode::Fan};
    constexpr Mode smart[]{Mode::Auto,Mode::Cool,Mode::Heat,Mode::Fan,Mode::Dry};
    if(family==Family::Hon)return value<sizeof(hon)/sizeof(*hon)?hon[value]:Mode::Unknown;
    return value<sizeof(smart)/sizeof(*smart)?smart[value]:Mode::Unknown;
}
void apply_bit(uint8_t& byte,uint8_t mask,bool on) {
    if(on)byte|=mask;
    else byte&=static_cast<uint8_t>(~mask);
}
} // namespace

uint16_t Frame::subtype() const {
    return size>=2?static_cast<uint16_t>((data[0]<<8)|data[1]):0;
}
void Reader::reset() {used_=0;separators_=0;escaped_=false;}
std::optional<Frame> Reader::finish() {
    const size_t length=body_[0];
    const bool crc=(body_[1]&crc_flag)!=0;
    if(checksum(body_.data(),length)!=body_[length])return {};
    if(crc && crc16(body_.data(),length)!=((body_[length+1]<<8)|body_[length+2]))return {};
    Frame frame;
    frame.type=static_cast<Type>(body_[7]);
    frame.crc=crc;
    frame.size=length-header_size;
    std::copy_n(body_.data()+header_size,frame.size,frame.data.begin());
    return frame;
}
std::optional<Frame> Reader::append(uint8_t byte) {
    if(separators_<2) {
        separators_=byte==separator?separators_+1:0;
        return {};
    }
    if(used_==0 && byte==separator)return {};
    if(escaped_) {
        escaped_=false;
        if(byte!=escape) {
            reset();
            if(byte==separator)separators_=2;
            return {};
        }
        byte=separator;
    } else if(byte==separator) {
        escaped_=true;
        return {};
    }
    if(used_==0 && (byte<header_size || byte>header_size+max_payload)) {
        reset();
        return {};
    }
    body_[used_++]=byte;
    if(used_<2 || used_!=body_[0]+1u+((body_[1]&crc_flag)?2u:0u))return {};
    auto frame=finish();
    reset();
    return frame;
}
std::optional<Packet> encode(Type type,bool crc,const uint8_t* data,size_t size) {
    if(size>max_payload || (size && !data))return {};
    std::array<uint8_t,max_payload+header_size+3> body{};
    const size_t length=header_size+size;
    body[0]=static_cast<uint8_t>(length);
    body[1]=crc?crc_flag:0;
    body[7]=static_cast<uint8_t>(type);
    if(size)std::copy_n(data,size,body.begin()+header_size);
    body[length]=checksum(body.data(),length);
    size_t total=length+1;
    if(crc) {
        const auto value=crc16(body.data(),length);
        body[total++]=static_cast<uint8_t>(value>>8);
        body[total++]=static_cast<uint8_t>(value);
    }
    Packet result;
    result.bytes[result.size++]=separator;
    result.bytes[result.size++]=separator;
    for(size_t i=0;i<total;++i) {
        result.bytes[result.size++]=body[i];
        if(body[i]==separator)result.bytes[result.size++]=escape;
    }
    return result;
}
Packet version_query() {
    constexpr uint8_t capabilities[]{0,7};
    return *encode(Type::Version,false,capabilities,sizeof(capabilities));
}
Packet query(Type type,bool crc,uint16_t subtype) {
    const uint8_t sub[]{static_cast<uint8_t>(subtype>>8),static_cast<uint8_t>(subtype)};
    return *encode(type,crc,sub,subtype?sizeof(sub):0);
}
std::optional<bool> hon_crc(const Frame& frame) {
    if(frame.type!=Type::VersionReply || frame.size<38 || frame.data[0]<0x20 || frame.data[0]>0x7e)return {};
    return (frame.data[37]&4)!=0;
}
std::optional<Climate> climate(const Frame& reply,Family family,bool crc) {
    if(reply.type!=Type::Status || reply.subtype()!=status_reply || reply.crc!=crc)return {};
    Climate out;out.family=family;out.crc=crc;
    const auto* c=reply.data.data()+2;
    if(family==Family::Hon) {
        if(reply.size!=hon_status_size)return {};
        std::copy_n(c,hon_control_size,out.wire.begin());
        out.mode=operating_mode(family,c[2]>>5);
        out.power=(c[5]&1)!=0;
        out.cooling_only=(c[13]&0x80)!=0;
        out.protected_mode=(c[4]&0x81) || (c[9]&0x10) || c[14]!=0;
        out.fahrenheit=(c[4]&0x20)!=0;
        if(!out.fahrenheit && c[0]<=14)out.target_c=16+c[0]+((c[4]&4)?.5:0);
        if(c[10]>=2 && c[10]<=100)out.indoor_c=c[10]/2.0;
        if(c[12]>0 && c[12]<=130)out.outdoor_c=static_cast<int>(c[12])-64;
    } else {
        if(reply.size!=2+smart_control_size || c[5]!=0x7f)return {};
        std::copy_n(c,smart_control_size,out.wire.begin());
        out.mode=operating_mode(family,c[11]);out.power=(c[17]&1)!=0;
        out.protected_mode=(c[17]&0x40)!=0;
        out.fahrenheit=(c[16]&8)!=0;
        if(!out.fahrenheit && c[23]<=14)out.target_c=16+c[23]+((c[17]&0x20)?.5:0);
        if(c[1]>=1 && c[1]<=50)out.indoor_c=c[1];
    }
    if(out.mode==Mode::Unknown)return {};
    return out;
}
bool same_settings(const Climate& a,const Climate& b) {
    return a.family==b.family && a.mode==b.mode && a.power==b.power && a.target_c==b.target_c &&
           a.cooling_only==b.cooling_only && a.protected_mode==b.protected_mode && a.fahrenheit==b.fahrenheit;
}
std::optional<Packet> command(const Climate& current,const Patch& patch,const PumpHeatingLimits& limits) {
    if(!limits.heating || current.mode==Mode::Unknown || current.cooling_only || current.protected_mode ||
       current.fahrenheit || (!patch.power && !patch.heat && !patch.target_c) ||
       (patch.heat && !*patch.heat) || (patch.power==false && (patch.heat || patch.target_c)) ||
       (patch.power==true && !patch.heat && current.mode!=Mode::Heat) ||
       (patch.target_c && !patch.heat && current.mode!=Mode::Heat))return {};
    if(patch.target_c && (!std::isfinite(*patch.target_c) || *patch.target_c<16 || *patch.target_c>30 ||
                         !limits.accepts(*patch.target_c) ||
                         *patch.target_c!=std::floor(*patch.target_c)))return {};
    auto settings=current.wire;
    uint16_t sub=0x6001;
    size_t size=hon_control_size;
    if(current.family==Family::Hon) {
        if(patch.power)apply_bit(settings[5],1,*patch.power);
        if(patch.heat)settings[2]=(settings[2]&0x1f)|0x80;
        if(patch.target_c) {
            settings[0]=static_cast<uint8_t>(*patch.target_c-16);
            apply_bit(settings[4],4,false);
        }
    } else {
        if(patch.power==false)return query(Type::Control,false,0x4d03);
        sub=0x4d5f;size=smart_control_size;
        settings[5]=0;
        if(patch.power)apply_bit(settings[17],1,*patch.power);
        if(patch.heat)settings[11]=2;
        if(patch.target_c) {
            settings[23]=static_cast<uint8_t>(*patch.target_c-16);
            apply_bit(settings[17],0x20,false);
        }
    }
    std::array<uint8_t,2+smart_control_size> payload{};
    payload[0]=static_cast<uint8_t>(sub>>8);payload[1]=static_cast<uint8_t>(sub);
    std::copy_n(settings.begin(),size,payload.begin()+2);
    return encode(Type::Control,current.crc,payload.data(),2+size);
}
bool readback_matches(const Climate& before,const Climate& after,const Patch& patch) {
    return before.family==after.family && !after.cooling_only && !after.protected_mode &&
           after.power==patch.power.value_or(before.power) &&
           after.mode==(patch.heat?Mode::Heat:before.mode) &&
           (!(patch.target_c || patch.power!=false) ||
                after.target_c==(patch.target_c?patch.target_c:before.target_c));
}
} // namespace enevina::haier
