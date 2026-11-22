#include "pump/haier_uart.hpp"
#include <array>
#include <cassert>
#include <cmath>
using namespace enevina;
using namespace enevina::haier;

static Frame read(const Packet& packet) {
    Reader reader;
    std::optional<Frame> decoded;
    for(size_t i=0;i<packet.size;++i) {
        if(auto frame=reader.append(packet.bytes[i]))decoded=*frame;
    }
    assert(decoded);
    return *decoded;
}
int main() {
    constexpr std::array<uint8_t,13> poll{0xff,0xff,0x0a,0,0,0,0,0,0,0x01,0x4d,0x01,0x59};
    const auto request=query(Type::Control,false,read_status);
    assert(request.size==poll.size());
    for(size_t i=0;i<poll.size();++i)assert(request.bytes[i]==poll[i]);

    const uint8_t escaped_payload[]{0xff,0x55,0xff};
    const auto escaped=*encode(Type::VersionReply,true,escaped_payload,sizeof(escaped_payload));
    const auto received=read(escaped);
    assert(received.crc && received.size==3 && received.data[0]==0xff && received.data[1]==0x55);
    auto corrupt=escaped;
    corrupt.bytes[corrupt.size-1]^=1;
    Reader reader;
    for(size_t i=0;i<corrupt.size;++i)assert(!reader.append(corrupt.bytes[i]));
    assert(!reader.append(0xff));
    for(size_t i=0;i<request.size-1;++i)assert(!reader.append(request.bytes[i]));
    assert(reader.append(request.bytes[request.size-1])->subtype()==read_status);

    std::array<uint8_t,34> hon{};
    hon[0]=0x6d;hon[1]=1;
    hon[2]=5;hon[4]=0x85;hon[6]=4;hon[7]=3;hon[9]=7;
    hon[12]=41;hon[14]=58;
    auto hReply=read(*encode(Type::Status,true,hon.data(),hon.size()));
    auto h=climate(hReply,Family::Hon,true);
    assert(h && h->mode==Mode::Heat && h->power && h->target_c==21.5 && h->indoor_c==20.5 && h->outdoor_c==-6);
    assert(!climate(hReply,Family::SmartAir2,false) && !climate(hReply,Family::Hon,false));
    hReply.data[1]=0x5f;
    assert(!climate(hReply,Family::Hon,true)); // the write reply cannot complete the fresh poll
    hReply.data[1]=1;
    hReply.data[16]=1;
    assert(!command(*climate(hReply,Family::Hon,true),Patch{false,{},{}},{16,30,1,true}));
    hReply.data[16]=0;
    hReply.data[15]=0x80;
    assert(!command(*climate(hReply,Family::Hon,true),Patch{true,true,22},{16,30,1,true}));

    const PumpHeatingLimits limits{16,30,1,true};
    const auto hSet=read(*command(*h,Patch{{},{},22},limits));
    assert(hSet.subtype()==0x6001 && hSet.crc && hSet.size==12);
    assert(hSet.data[2]==6 && hSet.data[4]==0x85 && hSet.data[7]==3 && hSet.data[9]==7);
    assert(!command(*h,Patch{{},{},22.5},limits));
    assert(!readback_matches(*h,*h,Patch{{},{},22}));
    auto after=*h;after.target_c=22;
    assert(readback_matches(*h,after,Patch{{},{},22}));
    after.power=false;
    assert(!readback_matches(*h,after,Patch{{},{},22}));

    std::array<uint8_t,26> smart{};
    smart[0]=0x6d;smart[1]=1;smart[3]=19;smart[7]=0x7f;
    smart[13]=2;smart[15]=3;smart[19]=1;smart[21]=0x12;smart[25]=5;
    auto s=climate(read(*encode(Type::Status,false,smart.data(),smart.size())),Family::SmartAir2,false);
    assert(s && s->mode==Mode::Heat && s->target_c==21 && s->indoor_c==19 && !s->outdoor_c);
    const auto off=read(*command(*s,Patch{false,{},{}},limits));
    assert(off.subtype()==0x4d03 && off.size==2);
    const auto sSet=read(*command(*s,Patch{true,true,24},limits));
    assert(sSet.subtype()==0x4d5f && sSet.size==26 && sSet.data[7]==0);
    assert(sSet.data[21]==0x12 && sSet.data[25]==8 && sSet.data[15]==3);
    auto outside=*s;outside.indoor_c=21;
    assert(same_settings(*s,outside));
    outside.mode=Mode::Cool;
    assert(!same_settings(*s,outside));
    std::array<uint8_t,38> version{};
    version[0]='E';version[37]=4;
    assert(hon_crc(read(*encode(Type::VersionReply,false,version.data(),version.size())))==true);
}
