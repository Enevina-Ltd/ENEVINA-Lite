#include "cottage/ruuvi_v5.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace enevina::cottage {
namespace {
uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0])<<8)|p[1]);
}
int16_t signed_be16(const uint8_t* p) {
    const uint16_t raw=be16(p);
    return raw<32768?static_cast<int16_t>(raw):
        static_cast<int16_t>(static_cast<int32_t>(raw)-65536);
}
int hex_digit(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
bool valid_mac(const std::array<uint8_t,6>& a) {
    bool all_zero=true,all_ff=true;
    for(uint8_t v:a) {all_zero &= v==0;all_ff &= v==255;}
    return !all_zero&&!all_ff;
}
}
bool parse_ruuvi_mac(const char* text,std::array<uint8_t,6>& out) {
    if(!text || std::strlen(text)!=17)return false;
    std::array<uint8_t,6> parsed{};
    for(size_t i=0;i<6;i++) {
        if(i && text[3*i-1]!=':')return false;
        const int a=hex_digit(text[3*i]),b=hex_digit(text[3*i+1]);
        if(a<0||b<0)return false;
        parsed[i]=static_cast<uint8_t>((a<<4)|b);
    }
    if(!valid_mac(parsed))return false;
    out=parsed;return true;
}
bool decode_ruuvi_v5(const uint8_t* d,size_t n,int rssi,RuuviV5& out) {
    // Exactly 2 company bytes + 24 DF5 bytes; no truncated/extra frames.
    if(!d || n!=26 || d[0]!=0x99 || d[1]!=0x04 || d[2]!=0x05)return false;
    const uint16_t temperature=be16(d+3),humidity=be16(d+5);
    const uint16_t pressure=be16(d+7),battery_tx=be16(d+15);
    const uint16_t seq=be16(d+18);
    const unsigned battery_raw=battery_tx>>5,tx_raw=battery_tx&31;
    if(temperature==0x8000 || humidity==0xffff || pressure==0xffff ||
       battery_raw==2047 || tx_raw==31 || d[17]==255 || seq==65535 ||
       rssi>20 || rssi<-120)return false;
    RuuviV5 sample{};
    std::copy(d+20,d+26,sample.mac.begin());
    if(!valid_mac(sample.mac))return false;
    sample.temperature_c=signed_be16(d+3)*0.005;
    sample.relative_humidity_pct=humidity*0.0025;
    sample.pressure_pa=50000+pressure;
    sample.battery_v=(1600+battery_raw)/1000.0;
    sample.tx_power_dbm=-40+static_cast<int>(tx_raw)*2;
    sample.rssi_dbm=rssi;
    sample.movement_count=d[17];
    sample.sequence=seq;
    // Reject implausible physical readings rather than blindly storing
    // the protocol's wide representable 163.835 C/163.835% extremes.
    if(sample.temperature_c<-45||sample.temperature_c>75 ||
       sample.relative_humidity_pct>100||sample.pressure_pa<50000 ||
       sample.pressure_pa>115534 || !std::isfinite(sample.temperature_c))
        return false;
    out=sample;
    return true;
}
void RuuviBinding::bind(const std::array<uint8_t,6>& mac) {
    bound_=valid_mac(mac);bound_mac_=mac;has_sample_=false;accepted_ms_=0;
}
void RuuviBinding::unbind() {
    bound_=false;has_sample_=false;accepted_ms_=0;bound_mac_={};
}
RuuviAccept RuuviBinding::accept(const RuuviV5& sample,uint64_t ms) {
    if(!bound_)return RuuviAccept::Unbound;
    if(sample.mac!=bound_mac_)return RuuviAccept::DifferentSensor;
    if(!std::isfinite(sample.temperature_c) ||
       !std::isfinite(sample.relative_humidity_pct)||
       sample.relative_humidity_pct<=0||sample.relative_humidity_pct>100||
       !std::isfinite(sample.pressure_pa)||sample.pressure_pa<50000||
       sample.pressure_pa>115534||!std::isfinite(sample.battery_v)||
       sample.battery_v<1.6||sample.battery_v>3.646)
        return RuuviAccept::Invalid;
    if(has_sample_) {
        if(ms<accepted_ms_)return RuuviAccept::Invalid;
        const unsigned distance=(static_cast<unsigned>(sample.sequence)+
           65535-static_cast<unsigned>(latest_.sequence))%65535;
        if(distance==0)return RuuviAccept::Duplicate;
        if(distance>32767)return RuuviAccept::OldSequence;
        const uint64_t elapsed_ms=ms-accepted_ms_;
        if(elapsed_ms<300000 &&
           std::fabs(sample.temperature_c-latest_.temperature_c)>5)
            return RuuviAccept::Implausible;
        if(elapsed_ms<60000 &&
           std::fabs(sample.relative_humidity_pct-
                     latest_.relative_humidity_pct)>20)
            return RuuviAccept::Implausible;
    }
    latest_=sample;accepted_ms_=ms;has_sample_=true;
    return RuuviAccept::Accepted;
}
bool RuuviBinding::valid(uint64_t ms,uint64_t age_ms_limit)const {
    return bound_&&has_sample_&&ms>=accepted_ms_&&
           ms-accepted_ms_<=age_ms_limit;
}
uint64_t RuuviBinding::age_ms(uint64_t ms)const {
    if(!has_sample_ || ms<accepted_ms_)return UINT64_MAX;
    return ms-accepted_ms_;
}
}
