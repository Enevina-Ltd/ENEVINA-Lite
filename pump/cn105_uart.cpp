#include "pump/cn105_uart.hpp"

namespace enevina::cn105_uart {
namespace {
constexpr uint8_t start_byte=0xfc,frame_version=1;
constexpr size_t header=5;
uint8_t checksum(const Frame& f) {
    uint8_t value=start_byte;
    for(size_t i=0;i+1<f.size;++i)value-=f.bytes[i];
    return value;
}
Frame frame(uint8_t type,size_t size) {
    Frame out{};
    out.size=header+size+1;
    out.bytes[0]=start_byte;out.bytes[1]=type;out.bytes[2]=frame_version;
    out.bytes[3]=heat_pump_family;out.bytes[4]=static_cast<uint8_t>(size);
    return out;
}
bool matches_query(const Frame& f,Query tag) {
    return response(f,command_data_reply) && f.bytes[4]==max_payload &&
        f.bytes[header]==static_cast<uint8_t>(tag);
}
Mode decode_mode(uint8_t mode) {
    if(mode>8)mode-=8; // The independent i-See bit also appears in some settings replies.
    switch(mode) {
    case 1:return Mode::Heat;
    case 2:return Mode::Dry;
    case 3:return Mode::Cooling;
    case 7:return Mode::Fan;
    case 8:return Mode::Auto;
    default:return Mode::Unknown;
    }
}
std::optional<double> celsius(uint8_t raw,double low,double high) {
    const double value=(static_cast<int>(raw)-128)/2.;
    return value>=low && value<=high?std::optional<double>{value}:std::nullopt;
}
}

std::optional<Frame> FrameReader::append(uint8_t byte) {
    if(partial_.size==0 || (partial_.size==1 && byte==start_byte)) {
        partial_.size=0;
        if(byte==start_byte)partial_.bytes[partial_.size++]=byte;
        return {};
    }
    partial_.bytes[partial_.size++]=byte;
    if((partial_.size==3 && byte!=frame_version) ||
       (partial_.size==4 && byte!=heat_pump_family) ||
       (partial_.size==5 && byte>max_payload)) {
        partial_.size=0;
        if(byte==start_byte)partial_.bytes[partial_.size++]=byte;
        return {};
    }
    if(partial_.size>=header && partial_.size==header+partial_.bytes[4]+1) {
        const Frame result=partial_;
        partial_.size=0;
        if(response(result,result.bytes[1]))return result;
    }
    return {};
}
Frame connect() {
    auto out=frame(0x5a,2);
    out.bytes[5]=0xca;out.bytes[6]=1;
    out.bytes[out.size-1]=checksum(out);
    return out;
}
Frame query(Query tag) {
    auto out=frame(0x42,max_payload);
    out.bytes[header]=static_cast<uint8_t>(tag);
    out.bytes[out.size-1]=checksum(out);
    return out;
}
bool response(const Frame& f,uint8_t type) {
    return f.size>=header+1 && f.size<=f.bytes.size() && f.bytes[0]==start_byte &&
        f.bytes[1]==type && f.bytes[2]==frame_version && f.bytes[3]==heat_pump_family &&
        f.size==header+f.bytes[4]+1 && f.bytes[f.size-1]==checksum(f);
}
std::optional<Settings> settings(const Frame& f) {
    if(!matches_query(f,Query::Settings))return {};
    const auto* data=f.bytes.data()+header;
    Settings out;
    if(data[3]<=1)out.power=data[3]==1;
    out.mode=decode_mode(data[4]);
    if(data[11]!=0 && data[11]!=0x80) {
        out.target_c=celsius(data[11],10,35);
        if(out.target_c)out.encoding=TemperatureEncoding::HalfDegree;
    } else if(data[11]==0 && data[5]<=15) {
        out.target_c=31-data[5];
        out.encoding=TemperatureEncoding::WholeDegree;
    }
    return out;
}
std::optional<Room> room(const Frame& f) {
    if(!matches_query(f,Query::Room))return {};
    const auto* data=f.bytes.data()+header;
    Room out;
    if(data[6]!=0)out.indoor_c=celsius(data[6],-10,55);
    else if(data[3]<=31)out.indoor_c=10+data[3];
    if(data[5]>1)out.outdoor_c=celsius(data[5],-55,63);
    return out;
}
std::optional<Operation> operation(const Frame& f) {
    if(!matches_query(f,Query::Operating))return {};
    const auto* data=f.bytes.data()+header;
    Operation out;
    if(data[4]<=1)out.running=data[4]==1;
    if(out.running==true && data[3]<=200)out.compressor_hz=data[3];
    return out;
}
std::optional<Frame> heat_command(const Settings& before,const Patch& patch,const PumpHeatingLimits& limits) {
    if(!limits.heating || (!patch.power && !patch.target_c && !patch.heat) ||
       (patch.power==false && (!limits.off || patch.target_c || patch.heat)) ||
       (patch.power==true && !patch.heat) || (patch.heat && (!patch.target_c || patch.power!=true)) ||
       (patch.target_c && !(patch.heat || before.mode==Mode::Heat)))return {};
    auto out=frame(0x41,max_payload);
    out.bytes[5]=1;
    if(patch.power) {out.bytes[6]|=1;out.bytes[8]=*patch.power?1:0;}
    if(patch.heat) {out.bytes[6]|=2;out.bytes[9]=1;}
    if(patch.target_c) {
        if(!limits.accepts(*patch.target_c))return {};
        const double halves=*patch.target_c*2.;
        if(std::fabs(halves-std::round(halves))>.001)return {};
        if(before.encoding==TemperatureEncoding::HalfDegree && *patch.target_c>=10 && *patch.target_c<=35)
            out.bytes[19]=static_cast<uint8_t>(std::lround(halves)+128);
        else if(before.encoding==TemperatureEncoding::WholeDegree && *patch.target_c>=16 &&
                *patch.target_c<=31 && std::fabs(*patch.target_c-std::round(*patch.target_c))<.001)
            out.bytes[10]=static_cast<uint8_t>(31-std::lround(*patch.target_c));
        else return {};
        out.bytes[6]|=4;
    }
    out.bytes[out.size-1]=checksum(out);
    return out;
}
bool readback_matches(const Settings& before,const Settings& after,const Patch& patch) {
    return (patch.power || patch.heat || patch.target_c) &&
        (patch.power?after.power==patch.power:after.power==before.power && after.power.has_value()) &&
        after.mode==(patch.heat?Mode::Heat:before.mode) &&
        (!patch.target_c || (after.target_c && std::fabs(*after.target_c-*patch.target_c)<.001));
}
PumpRoomMode heating_mode(const Settings& s) {
    if(s.power==false)return PumpRoomMode::Off;
    if(!s.power || s.mode==Mode::Unknown || (s.mode==Mode::Heat && !s.target_c))return PumpRoomMode::Unknown;
    return s.mode==Mode::Heat?PumpRoomMode::Heat:PumpRoomMode::Other;
}
}
