#include "pump/midea_discovery_info.hpp"
#include "pump/midea_v3_protocol.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace enevina::midea;

int main() {
    // Synthetic public data at the offsets specified by midea-local discover.py.
    const char ssid[]="net_ac_TEST";
    Bytes info(80,0);
    info[4]=0x2c;info[5]=0x19; // TCP 6444, little endian
    std::memcpy(info.data()+17,"22013279",8);
    info[40]=sizeof(ssid)-1;
    std::memcpy(info.data()+41,ssid,sizeof(ssid)-1);
    info[57+sizeof(ssid)-1]=0x05;
    info[58+sizeof(ssid)-1]=0x80;
    Bytes packet;
    uint8_t stamp[8]{};
    assert(inner_encode(123456,info.data(),info.size(),stamp,packet));
    DiscoveryInfo parsed{};
    assert(parse_discovery_info(packet.data(),packet.size(),parsed));
    assert(parsed.device.id==123456 && parsed.device.protocol==2);
    assert(parsed.device_type==0xac && parsed.port==6444);
    assert(std::strcmp(parsed.model.data(),"22013279")==0);
    assert(parsed.subtype_known && parsed.subtype==32773);
    Bytes v3{0x83,0x70,0,0,0x20,0,0,0};
    v3.insert(v3.end(),packet.begin(),packet.end());v3.resize(v3.size()+16,0);
    v3[2]=static_cast<uint8_t>((v3.size()-8)>>8);
    v3[3]=static_cast<uint8_t>(v3.size()-8);
    assert(parse_discovery_info(v3.data(),v3.size(),parsed));
    assert(parsed.device.protocol==3 && parsed.device.id==123456);
    auto original=parsed;
    packet[60]^=1;
    assert(!parse_discovery_info(packet.data(),packet.size(),parsed));
    assert(parsed.device.id==original.device.id && parsed.subtype==original.subtype);
    for(size_t len=0;len<v3.size();++len)
        assert(!parse_discovery_info(v3.data(),len,parsed));
    // A valid encrypted reply with an impossible SSID length is not metadata.
    info[40]=200;
    assert(inner_encode(123456,info.data(),info.size(),stamp,packet));
    assert(!parse_discovery_info(packet.data(),packet.size(),parsed));
    info[40]=sizeof(ssid)-1;info[4]=0;info[5]=0;
    assert(inner_encode(123456,info.data(),info.size(),stamp,packet));
    assert(!parse_discovery_info(packet.data(),packet.size(),parsed));
    info[4]=0x2c;info[5]=0x19;info[6]=1; // port above 65535
    assert(inner_encode(123456,info.data(),info.size(),stamp,packet));
    assert(!parse_discovery_info(packet.data(),packet.size(),parsed));
    info[6]=0;info[43]='_'; // invalid device type token
    assert(inner_encode(123456,info.data(),info.size(),stamp,packet));
    assert(!parse_discovery_info(packet.data(),packet.size(),parsed));
    std::cout<<"Midea discovery metadata and identity validation PASS\n";
}
