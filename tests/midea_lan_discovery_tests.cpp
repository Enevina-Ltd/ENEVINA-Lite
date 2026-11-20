#include "pump/midea_discovery.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

using enevina::midea::DiscoveredDevice;
using enevina::midea::parse_discovery_reply;

int main() {
    static_assert(enevina::midea::discovery_request.size()==72);
    assert(enevina::midea::discovery_request[0]==0x5a &&
           enevina::midea::discovery_request[1]==0x5a);
    constexpr uint64_t id=0x01a2b3c4d5e6ULL;
    auto build=[](uint8_t protocol, uint64_t device_id) {
        std::array<uint8_t,208> packet{};
        const size_t offset=protocol==3?8:0;
        if(protocol==3){packet[0]=0x83;packet[1]=0x70;}
        packet[offset]=0x5a;packet[offset+1]=0x5a;
        for(size_t i=0;i<6;i++)packet[offset+20+i]=static_cast<uint8_t>(device_id>>(8*i));
        return packet;
    };
    DiscoveredDevice d{99,99};
    auto p3=build(3,id);
    assert(parse_discovery_reply(p3.data(),p3.size(),d));
    assert(d.protocol==3 && d.id==id);
    auto p2=build(2,id);
    assert(parse_discovery_reply(p2.data(),184,d));
    assert(d.protocol==2 && d.id==id);
    assert(!parse_discovery_reply(nullptr,0,d));
    assert(d.protocol==0 && d.id==0);
    assert(!parse_discovery_reply(p3.data(),72,d));
    auto invalid=build(3,id);invalid[8]=0x00;
    assert(!parse_discovery_reply(invalid.data(),invalid.size(),d));
    auto invalid_id=build(3,0);
    assert(!parse_discovery_reply(invalid_id.data(),invalid_id.size(),d));
    auto invalid_prefix=build(3,id);invalid_prefix[1]=0xff;
    assert(!parse_discovery_reply(invalid_prefix.data(),invalid_prefix.size(),d));
    assert(!parse_discovery_reply(p3.data(),1025,d));
    std::cout<<"Midea LAN V2/V3 discovery parser 10/10 PASS\n";
}
