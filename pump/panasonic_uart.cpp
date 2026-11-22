#include "pump/panasonic_uart.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace enevina::panasonic {
namespace {
constexpr uint8_t cnt_read=0x70,cnt_write=0xf0,wlan=0x5a;
void checksum(Packet& packet) {
    uint8_t sum=0;
    for(size_t i=0;i+1<packet.size;++i)sum+=packet.bytes[i];
    packet.bytes[packet.size-1]=static_cast<uint8_t>(0-sum);
}
Packet cnt_packet(uint8_t header,const std::array<uint8_t,10>& data) {
    Packet p;p.size=13;p.bytes[0]=header;p.bytes[1]=data.size();
    std::copy(data.begin(),data.end(),p.bytes.begin()+2);
    checksum(p);
    return p;
}
Packet wlan_packet(uint8_t number,uint8_t group,uint8_t code,std::initializer_list<uint8_t> data) {
    Packet p;p.size=data.size()+7;
    p.bytes[0]=wlan;p.bytes[1]=number;p.bytes[2]=group;p.bytes[3]=code;
    p.bytes[5]=static_cast<uint8_t>(data.size());
    std::copy(data.begin(),data.end(),p.bytes.begin()+6);
    checksum(p);
    return p;
}
Mode cnt_mode(uint8_t value) {
    switch(value>>4) {
        case 0:return Mode::Auto;
        case 2:return Mode::Dry;
        case 3:return Mode::Cool;
        case 4:return Mode::Heat;
        case 6:return Mode::Fan;
        default:return Mode::Unknown;
    }
}
Mode wlan_mode(uint8_t value) {
    switch(value) {
        case 0x41:return Mode::Auto;
        case 0x42:return Mode::Cool;
        case 0x43:return Mode::Heat;
        case 0x44:return Mode::Dry;
        case 0x45:return Mode::Fan;
        default:return Mode::Unknown;
    }
}
std::optional<double> temperature(uint8_t value,int low,int high) {
    if(value==0x80)return {};
    const int signed_c=value<128?value:static_cast<int>(value)-256;
    if(signed_c<low || signed_c>high)return {};
    return signed_c;
}
std::optional<double> target(uint8_t value) {
    if(value<20 || value>70)return {};
    return value/2.0;
}
} // namespace

bool Packet::is_wlan(uint8_t group,uint8_t code) const {
    return size>=7 && bytes[0]==wlan && bytes[2]==group && bytes[3]==code;
}
std::optional<Packet> Reader::append(uint8_t byte) {
    auto& p=pending_;
    if(!p.size && byte!=cnt_read && byte!=cnt_write && byte!=wlan)return {};
    p.bytes[p.size++]=byte;
    const bool is_wlan=p.bytes[0]==wlan;
    if(!is_wlan && p.size==2)expected_=3u+p.bytes[1];
    if(is_wlan && p.size==6)expected_=7u+(p.bytes[4]<<8)+p.bytes[5];
    if(expected_>p.bytes.size()) {p.size=expected_=0;return {};}
    if(!expected_ || p.size<expected_)return {};
    uint8_t sum=0;
    for(size_t i=0;i<p.size;++i)sum+=p.bytes[i];
    const auto out=p;
    p.size=expected_=0;
    if(sum!=0)return {};
    return out;
}
uint8_t next_sequence(uint8_t number) {return number>=0xfe?1:static_cast<uint8_t>(number+1);}
Packet cnt_poll() {return cnt_packet(cnt_read,{});}
Packet wlan_poll(uint8_t sequence) {
    return wlan_packet(sequence,0x10,0x09,{
        0x01,0x01,0x30,0x01,0x11,0x00,0x80,0,0,0xb0,0,2,
        0x31,0,0,0xa0,0,0,0xa1,0,0,0xa5,0,0,0xa4,0,0,0xb2,0,2,
        0x35,0,2,0x33,0,2,0x34,0,2,0x32,0,0,0xbb,0,0,0xbe,0,2,
        0x20,0,2,0x21,0,0,0x86,0
    });
}
Packet wlan_reply(const Packet& request,const std::array<uint8_t,6>& mac) {
    const auto seq=request.sequence();
    if(request.is_wlan(1,1))return wlan_packet(seq,1,0x81,{0,0x11,0x12});
    if(request.is_wlan(0x10,0x0a))return wlan_packet(seq,0x10,0x8a,{0,1,0x30,1});
    if(request.is_wlan(1,9))return wlan_packet(seq,1,0x89,{0,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]});
    if(request.is_wlan(0,0x20))
        return wlan_packet(seq,0,0xa0,{0,8,'0','2','.','0','3','.','0','0',8,'0','1','0','1','0','1','0','3'});
    return {};
}
HandshakeStep wlan_handshake(unsigned step,uint8_t sequence) {
    switch(step) {
        case 1:return {wlan_packet(sequence,0,6,{})};
        case 2:return {wlan_packet(sequence,0,9,{}),0,0x89};
        case 3:return {wlan_packet(sequence,0,0x0c,{}),0,0x8c};
        case 4:return {wlan_packet(sequence,0,0x10,{0x20}),0,0x90};
        case 5:return {wlan_packet(sequence,0,0x11,{0,1}),0,0x91};
        case 6:return {wlan_packet(sequence,0,0x12,{1,0x10,0x11,0x12}),0,0x92};
        case 7:return {wlan_packet(sequence,0,0x41,{}),0,0xc1};
        case 8:return {wlan_packet(sequence,1,0x4c,{}),1,0xcc};
        case 9:return {wlan_packet(sequence,0x10,0,{}),0x10,0x80};
        case 10:return {wlan_packet(sequence,0x10,1,{1,0x30,1,0,1}),0x10,0x81};
        case 11:return {wlan_packet(sequence,0,0x18,{}),0,0x98};
        case 12:return {wlan_packet(sequence,1,0,{0x10}),1,0x80};
        case 13:return {wlan_packet(sequence,0x10,8,{1,1,0x30,1,1,2,0x42,1,0x42}),0x10,0x88};
        case 16:return {wlan_packet(sequence,1,0,{0x11}),1,0x80};
        default:{HandshakeStep none;none.packet.bytes[1]=sequence;return none;}
    }
}
std::optional<bool> defrost_status(const Packet& p) {
    if(p.size<16 || p.bytes[0]!=cnt_read)return {};
    if(p.bytes[14]==2)return true;
    if(p.bytes[14]==0 || p.bytes[14]==1)return false;
    return {};
}
std::optional<Climate> climate(const Packet& p,Family family,uint8_t sequence) {
    Climate out;out.family=family;
    if(family==Family::Cnt) {
        if(p.size<16 || p.bytes[0]!=cnt_read)return {};
        std::copy_n(p.bytes.begin()+2,out.cnt_control.size(),out.cnt_control.begin());
        const uint8_t mode=out.cnt_control[0];
        out.mode=cnt_mode(mode);out.power=(mode&0x0f)!=0;
        out.target_c=target(out.cnt_control[1]);
        const size_t payload_end=p.size-1;
        if(payload_end>18)out.indoor_c=temperature(p.bytes[18],-30,55);
        if(payload_end>19)out.outdoor_c=temperature(p.bytes[19],-65,65);
        if(!out.indoor_c && payload_end>21)out.indoor_c=temperature(p.bytes[21],-30,55);
        if(!out.outdoor_c && payload_end>22)out.outdoor_c=temperature(p.bytes[22],-65,65);
        out.defrost=defrost_status(p);
    } else {
        if(!p.is_wlan(0x10,0x89) || p.size!=125 || p.sequence()!=sequence ||
           (p.bytes[14]!=0x30 && p.bytes[14]!=0x31))return {};
        out.power=p.bytes[14]==0x30;out.mode=wlan_mode(p.bytes[18]);
        out.target_c=target(p.bytes[22]);
        out.indoor_c=temperature(p.bytes[62],-30,55);
        out.outdoor_c=temperature(p.bytes[66],-65,65);
    }
    if(out.mode==Mode::Unknown)return {};
    return out;
}
bool same_settings(const Climate& a,const Climate& b) {
    return a.family==b.family && a.mode==b.mode && a.power==b.power &&
           a.target_c==b.target_c && a.defrost==b.defrost;
}
std::optional<Packet> command(const Climate& before,const Patch& patch,const PumpHeatingLimits& limits,uint8_t sequence) {
    if(!limits.heating || before.mode==Mode::Unknown || before.defrost==true ||
       (!patch.power && !patch.heat && !patch.target_c) || (patch.heat && !*patch.heat) ||
       (patch.power==false && (!limits.off || patch.heat || patch.target_c)) ||
       (patch.power==true && !patch.heat && before.mode!=Mode::Heat) ||
       (patch.target_c && !patch.heat && before.mode!=Mode::Heat))return {};
    if(patch.target_c && (!limits.accepts(*patch.target_c) ||
       *patch.target_c<16 || *patch.target_c>30 || *patch.target_c*2!=std::floor(*patch.target_c*2)))return {};
    if(before.family==Family::Cnt) {
        auto data=before.cnt_control;
        if(patch.heat)data[0]=(data[0]&0x0f)|0x40;
        if(patch.power)data[0]=(data[0]&0xf0)|(*patch.power?0x04:0x00);
        if(patch.target_c)data[1]=static_cast<uint8_t>(*patch.target_c*2);
        return cnt_packet(cnt_write,data);
    }
    Packet out;out.size=12;
    out.bytes[0]=wlan;out.bytes[1]=sequence;out.bytes[2]=0x10;out.bytes[3]=8;
    out.bytes[6]=1;out.bytes[7]=1;out.bytes[8]=0x30;out.bytes[9]=1;
    const auto add=[&](uint8_t key,uint8_t value) {
        out.bytes[out.size]=key;out.bytes[out.size+1]=1;out.bytes[out.size+2]=value;
        out.size+=4;++out.bytes[10];
    };
    if(patch.heat)add(0xb0,0x43);
    if(patch.power)add(0x80,*patch.power?0x30:0x31);
    if(patch.target_c)add(0x31,static_cast<uint8_t>(*patch.target_c*2));
    out.bytes[5]=static_cast<uint8_t>(out.size-7);
    checksum(out);
    return out;
}
bool readback_matches(const Climate& before,const Climate& after,const Patch& patch) {
    return before.family==after.family && after.defrost!=true &&
           after.power==patch.power.value_or(before.power) &&
           after.mode==(patch.heat?Mode::Heat:before.mode) &&
           (patch.power==false || after.target_c==(patch.target_c?patch.target_c:before.target_c));
}
} // namespace enevina::panasonic
