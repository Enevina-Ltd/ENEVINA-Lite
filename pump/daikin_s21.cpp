#include "pump/daikin_s21.hpp"
#include <algorithm>

namespace enevina::s21 {
namespace {
bool framing_byte(uint8_t value) {
    return value==stx || value==etx || value==ack || value==nak;
}
uint8_t packet_check(const Frame& frame) {
    unsigned sum=0;
    for(size_t i=1;i+2<frame.size;++i)sum+=frame.bytes[i];
    const uint8_t low=static_cast<uint8_t>(sum);
    return framing_byte(low)?static_cast<uint8_t>(low+2):low;
}
bool complete(const Frame& f) {
    return f.size>=5 && f.size<=f.bytes.size() && f.bytes[0]==stx &&
        f.bytes[f.size-1]==etx && f.bytes[f.size-2]==packet_check(f);
}
Frame make(Code code,const std::array<uint8_t,4>* data) {
    Frame f{};
    f.bytes[0]=stx;f.bytes[1]=static_cast<uint8_t>(code[0]);f.bytes[2]=static_cast<uint8_t>(code[1]);
    f.size=data?9:5;
    if(data)std::copy(data->begin(),data->end(),f.bytes.begin()+3);
    f.bytes[f.size-2]=packet_check(f);
    f.bytes[f.size-1]=etx;
    return f;
}
}
std::optional<Frame> Reader::append(uint8_t byte) {
    if(byte==stx) {
        frame_.size=1;frame_.bytes[0]=byte;
        return {};
    }
    if(!frame_.size)return {};
    if(frame_.size==frame_.bytes.size()) {frame_.size=0;return {};}
    frame_.bytes[frame_.size++]=byte;
    if(byte!=etx)return {};
    const auto finished=frame_;
    frame_.size=0;
    return complete(finished)?std::optional<Frame>{finished}:std::nullopt;
}
Frame query(Code cmd) {return make(cmd,nullptr);}
bool response(const Frame& frame,Code cmd,size_t payload_size) {
    return complete(frame) && frame.size==5+payload_size &&
        frame.bytes[1]==static_cast<uint8_t>(cmd[0]) && frame.bytes[2]==static_cast<uint8_t>(cmd[1]);
}
std::optional<Climate> climate(const Frame& frame) {
    if(!response(frame,climate_reply,4))return {};
    Climate r{};
    std::copy_n(frame.bytes.begin()+3,4,r.wire.begin());
    if((r.wire[0]&0x30)!=0x30 || r.wire[1]<'0' || r.wire[1]>'7')return {};
    const auto mode=static_cast<Mode>(r.wire[1]-'0');
    if(mode==Mode::Auto || mode==Mode::Dry || mode==Mode::Cooling ||
       mode==Mode::Heat || mode==Mode::Fan)r.mode=mode;
    r.power=(r.wire[0]&1)!=0;
    if(r.mode==Mode::Heat || r.mode==Mode::Auto || r.mode==Mode::Cooling) {
        const double value=18.+(static_cast<int>(r.wire[2])-'@')*.5;
        if(value>=10 && value<=35)r.target_c=value;
    }
    return r;
}
std::optional<double> temperature(const Frame& frame,Code cmd) {
    if(!response(frame,cmd,4))return {};
    unsigned total=0,factor=1;
    for(size_t i=3;i<6;++i) {
        if(frame.bytes[i]<'0' || frame.bytes[i]>'9')return {};
        total+=(frame.bytes[i]-'0')*factor;
        factor*=10;
    }
    const char sign=static_cast<char>(frame.bytes[6]);
    if(sign!='+' && sign!='-')return {};
    const double value=total*.1*(sign=='-'?-1:1);
    return value>=-55 && value<=65?std::optional<double>{value}:std::nullopt;
}
std::optional<Frame> command(const Climate& before,const Patch& patch,const PumpHeatingLimits& limits) {
    if(!limits.heating || before.mode==Mode::Unknown ||
       (!patch.power && !patch.heat && !patch.target_c) ||
       (patch.power==false && (!limits.off || patch.heat || patch.target_c)) ||
       (patch.power==true && !patch.heat) || (patch.heat && (!patch.target_c || patch.power!=true)) ||
       (patch.target_c && !patch.heat && before.mode!=Mode::Heat))return {};
    auto values=before.wire;
    if(patch.power)values[0]=static_cast<uint8_t>((values[0]&0xfeu)|(*patch.power?1:0));
    if(patch.heat)values[1]='4';
    if(patch.target_c) {
        const auto target=*patch.target_c;
        const auto scaled=(target-18.)*2.;
        if(!limits.accepts(target) || target<10 || target>35 ||
           std::fabs(scaled-std::round(scaled))>.001)return {};
        values[2]=static_cast<uint8_t>('@'+std::lround(scaled));
    }
    if(std::any_of(values.begin(),values.end(),framing_byte))return {};
    return make(write_climate,&values);
}
bool readback_matches(const Climate& before,const Climate& after,const Patch& patch) {
    return (patch.power || patch.target_c || patch.heat) &&
        after.power==patch.power.value_or(before.power) &&
        after.mode==(patch.heat?Mode::Heat:before.mode) &&
        (!patch.target_c || (after.target_c && std::fabs(*after.target_c-*patch.target_c)<.001));
}
PumpRoomMode heating_mode(const Climate& climate) {
    if(!climate.power)return PumpRoomMode::Off;
    if(climate.mode==Mode::Unknown || (climate.mode==Mode::Heat && !climate.target_c))
        return PumpRoomMode::Unknown;
    return climate.mode==Mode::Heat?PumpRoomMode::Heat:PumpRoomMode::Other;
}
}
