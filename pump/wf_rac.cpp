#include "pump/wf_rac.hpp"
#include <array>

namespace enevina::wf_rac {
namespace {
constexpr size_t wire_state_size=18,member_count_offset=18,block_base_size=21;
constexpr uint8_t model_signature=0x80;
// These tables in tenths of a degree reproduce the MIT-licensed pywfrac
// temperature tables. The original notice is in third_party/pywfrac/LICENSE.
constexpr std::array<int16_t,256> indoor_temperature{
    -300,-300,-300,-300,-300,-300,-300,-300,-300,-300,-300,-300,-300,-300,-300,-300,
    -290,-280,-270,-260,-250,-240,-230,-225,-220,-210,-200,-195,-190,-180,-175,-170,
    -165,-160,-150,-145,-140,-135,-130,-125,-120,-115,-110,-105,-100,-95,-90,-86,
    -83,-80,-75,-70,-65,-60,-56,-53,-50,-45,-40,-36,-33,-30,-26,-23,
    -20,-16,-13,-10,-5,0,3,6,10,13,16,20,23,26,30,32,
    35,37,40,43,46,50,53,56,60,63,66,70,72,75,77,80,
    83,86,90,92,95,97,100,103,106,110,112,115,117,120,123,126,
    130,132,135,137,140,142,145,147,150,153,156,160,162,165,167,170,
    172,175,177,180,182,185,187,190,192,195,197,200,202,205,207,210,
    212,215,217,220,222,225,227,230,232,235,237,240,242,245,247,250,
    252,255,257,260,262,265,267,270,272,275,277,280,282,285,287,290,
    292,295,297,300,302,305,307,310,313,316,320,322,325,327,330,332,
    335,337,340,342,345,347,350,353,356,360,362,365,367,370,372,375,
    377,380,383,386,390,392,395,397,400,403,406,410,412,415,417,420,
    423,426,430,432,435,437,440,443,446,450,453,456,460,462,465,467,
    470,473,476,480,483,486,490,493,496,500,503,506,510,513,516,520,
};
constexpr std::array<int16_t,256> outdoor_temperature{
    -500,-500,-500,-500,-500,-489,-460,-440,-420,-410,-390,-380,-370,-360,-350,-340,
    -330,-320,-310,-300,-290,-285,-280,-270,-260,-255,-250,-240,-235,-230,-225,-220,
    -215,-210,-205,-200,-195,-190,-185,-180,-175,-170,-165,-160,-155,-150,-146,-143,
    -140,-135,-130,-126,-123,-120,-115,-110,-106,-103,-100,-96,-93,-90,-86,-83,
    -80,-76,-73,-70,-66,-63,-60,-56,-53,-50,-46,-43,-40,-37,-35,-32,
    -30,-26,-23,-20,-17,-15,-12,-10,-6,-3,0,2,5,7,10,13,
    16,20,22,25,27,30,32,35,37,40,42,45,47,50,52,55,
    57,60,62,65,67,70,72,75,77,80,82,85,87,90,92,95,
    97,100,102,105,107,110,112,115,117,120,122,125,127,130,132,135,
    137,140,142,144,146,148,150,152,155,157,160,162,165,167,170,172,
    175,177,180,182,185,187,190,192,194,196,198,200,202,205,207,210,
    212,215,217,220,222,225,227,230,232,235,237,240,242,245,247,250,
    252,255,257,260,262,265,267,270,272,275,277,280,282,285,287,290,
    292,295,297,300,302,305,307,310,313,316,320,322,325,327,330,332,
    335,337,340,343,346,350,352,355,357,360,363,366,370,372,375,377,
    380,383,386,390,393,396,400,403,406,410,413,416,420,423,426,430,
};

uint8_t byte(std::string_view text,size_t offset) {
    return static_cast<uint8_t>(text[offset]);
}
size_t block_size(std::string_view data) {
    return data.size()>=block_base_size?block_base_size+4u*byte(data,member_count_offset):0;
}
uint16_t footer(std::string_view data) {
    return byte(data,data.size()-2)|static_cast<uint16_t>(byte(data,data.size()-1))<<8;
}
bool full_crc(std::string_view data) {
    return crc16(data.substr(0,data.size()-2))==footer(data);
}
std::string framed_block(std::array<uint8_t,wire_state_size> state) {
    constexpr std::array<uint8_t,5> no_operation{1,255,255,255,255};
    std::string out(reinterpret_cast<const char*>(state.data()),state.size());
    out.append(reinterpret_cast<const char*>(no_operation.data()),no_operation.size());
    const auto sum=crc16(out);
    out.push_back(static_cast<char>(sum&0xff));
    out.push_back(static_cast<char>(sum>>8));
    return out;
}
}

std::optional<std::string> unit_id(std::string_view id) {
    if(id.size()!=12 && id.size()!=17)return {};
    const bool separated=id.size()==17;
    const char separator=separated?id[2]:0;
    if(separated && separator!=':' && separator!='-')return {};
    std::string result;
    for(size_t i=0;i<id.size();++i) {
        char c=id[i];
        if(separated && i%3==2) {
            if(c!=separator)return {};
            continue;
        }
        if(c>='A' && c<='F')c=static_cast<char>(c+'a'-'A');
        if(!(c>='0' && c<='9') && !(c>='a' && c<='f'))return {};
        result.push_back(c);
    }
    if(result=="000000000000" || result=="ffffffffffff")return {};
    return result;
}
bool operator_id_valid(std::string_view id) {
    if(id.size()!=36)return false;
    for(size_t i=0;i<id.size();++i) {
        const char c=id[i];
        if(i==8 || i==13 || i==18 || i==23) {
            if(c!='-')return false;
        } else if(!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F')))
            return false;
    }
    return true;
}
bool same_request(const Reading& a,const Reading& b) {
    return a.model_number==b.model_number && a.mode==b.mode && a.power==b.power &&
        a.target_c==b.target_c && a.fault==b.fault && a.self_clean==b.self_clean;
}
bool changes_mode(const Reading& before,const Patch& patch) {
    return before.power!=patch.power.value_or(before.power) || (patch.heat && before.mode!=Mode::Heat);
}
bool permits_heat(const Reading& before,const Patch& patch) {
    return before.fault==0 && !before.self_clean && before.mode!=Mode::Unknown &&
        (!patch.target_c || patch.heat || before.mode==Mode::Heat);
}
uint16_t crc16(std::string_view bytes) {
    uint16_t value=0xffff;
    for(const uint8_t b:bytes) {
        value^=static_cast<uint16_t>(b)<<8;
        for(unsigned bit=0;bit<8;++bit)
            value=static_cast<uint16_t>((value<<1) ^ (value&0x8000?0x1021:0));
    }
    return value;
}
std::optional<Reading> decode(std::string_view bytes) {
    if(bytes.size()>max_frame)return {};
    const size_t start=block_size(bytes);
    if(!start || start>bytes.size() || bytes.size()-start<block_base_size)return {};
    const auto sent=bytes.substr(0,start),received=bytes.substr(start);
    if(block_size(received)!=received.size() || !full_crc(sent))return {};
    const bool full=full_crc(received);
    // Some modules start the receive checksum on the previous block's CRC.
    // The last two payload bytes are unverified in that variant.
    if(!full && (start!=25 || crc16(bytes.substr(start-2,received.size()-2))!=footer(received)))
        return {};
    if(!(byte(received,0)&model_signature) || byte(received,1)!=0x04 ||
       (byte(received,2)&0x02)!=0)return {};
    Reading reading;
    reading.model_number=byte(received,0)&0x7f;
    reading.power=(byte(received,2)&1)!=0;
    const auto mode=(byte(received,2)&0x3c)>>2;
    reading.mode=mode<=4?static_cast<Mode>(mode):Mode::Unknown;
    const auto target=byte(received,4);
    if(target>=20 && target<=66)reading.target_c=target*.5;
    reading.fault=byte(received,6);
    reading.compressor_demand=(byte(received,9)&2)!=0;
    reading.self_clean=(byte(received,15)&1)!=0;
    const size_t verified_end=received.size()-(full?2:4);
    for(size_t offset=19;offset+2<verified_end;offset+=4) {
        if(byte(received,offset)!=0x80)continue;
        const auto channel=byte(received,offset+1),raw=byte(received,offset+2);
        if(channel==0x20 && raw==byte(received,5))reading.indoor_c=indoor_temperature[raw]/10.0;
        if(channel==0x10)reading.outdoor_c=outdoor_temperature[raw]/10.0;
    }
    return reading;
}
std::optional<std::string> command(const Patch& patch,const PumpHeatingLimits& limits) {
    if(!limits.heating || (!patch.power && !patch.target_c && !patch.heat) ||
       (patch.power==false && (!limits.off || patch.target_c || patch.heat)) ||
       (patch.power==true && !patch.heat) ||
       (patch.heat && (!patch.target_c || patch.power!=true)))return {};
    std::array<uint8_t,wire_state_size> payload{};
    payload[5]=0xff; // Leave the module's room temperature source in place.
    auto empty=payload;
    if(patch.power)payload[2]=*patch.power?3:2;
    if(patch.heat)payload[2]|=0x30;
    if(patch.target_c) {
        const double halves=*patch.target_c*2;
        if(!limits.accepts(*patch.target_c) || halves<20 || halves>66 ||
           std::fabs(halves-std::round(halves))>0.001)return {};
        payload[4]=static_cast<uint8_t>(std::lround(halves))|0x80;
    }
    return framed_block(payload)+framed_block(empty);
}
bool readback_matches(const Reading& before,const Reading& after,const Patch& patch) {
    return (patch.power || patch.target_c || patch.heat) && after.model_number==before.model_number &&
        after.power==patch.power.value_or(before.power) && after.mode==(patch.heat?Mode::Heat:before.mode) &&
        (!patch.target_c || (after.target_c && std::fabs(*after.target_c-*patch.target_c)<0.001));
}
PumpRoomMode heating_mode(const Reading& reading) {
    if(!reading.power)return PumpRoomMode::Off;
    if(reading.mode==Mode::Unknown || (reading.mode==Mode::Heat && !reading.target_c))
        return PumpRoomMode::Unknown;
    return reading.mode==Mode::Heat?PumpRoomMode::Heat:PumpRoomMode::Other;
}
}
