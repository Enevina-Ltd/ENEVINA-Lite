#include "pump/midea_v3_protocol.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
using namespace enevina::midea;

static Bytes from_hex(const char* input) {
    const size_t chars=std::strlen(input);
    assert(chars%2==0);
    Bytes bytes(chars/2);
    assert(decode_hex(input,bytes.data(),bytes.size()));
    return bytes;
}
static uint8_t crc8_midea(const Bytes& p) {
    uint8_t crc=0;
    for(uint8_t value:p) {
        crc^=value;
        for(int b=0;b<8;++b)
            crc=(crc&1)?uint8_t((crc>>1)^0x8c):uint8_t(crc>>1);
    }
    return crc;
}
static Bytes state_reply_frame() {
    // Invented C0 status for HEAT 22 C, indoor 20 C and absent outdoor.
    Bytes p(22,0);
    p[0]=0xc0;p[1]=1;p[2]=uint8_t(4u<<5|6u);
    p[3]=102;p[7]=1;p[11]=90;p[12]=0xff;
    p.push_back(crc8_midea(p));
    Bytes frame(10,0);
    frame[0]=0xaa;
    frame[1]=uint8_t(10+p.size());
    frame[2]=0xac;
    frame[9]=3;
    frame.insert(frame.end(),p.begin(),p.end());
    uint8_t sum=0;
    for(size_t i=1;i<frame.size();++i)sum=uint8_t(sum+frame[i]);
    frame.push_back(uint8_t(-sum));
    return frame;
}
int main() {
    uint64_t id=0;
    assert(decode_device_id("123456",id) && id==123456);
    for(const auto* v: {"","0","-1","+1","123X","18446744073709551616"}) {
        assert(!decode_device_id(v,id));
    }
    assert(!decode_device_id(nullptr,id));
    assert(decode_device_id("18446744073709551615",id));
    // Verified independently with msmart-ng's _Packet.encode()
    // (synthetic ID and UTC timestamp, not a real heat pump).
    const Bytes reference=from_hex(
        "5a5a0111680020000000000000040302010b1a1440e201000000000000000000000000000000000065cd8728fc6a5d31e6464235e81fa3b3e10c552981b23022cb71ea0fc54dc25e29ff4bed361a3a2fbe593a9d1a721035eba6281bfc401cb00b6c2e5b18452ebd");
    const uint8_t stamp[8]{0,4,3,2,1,11,26,20};
    const Bytes query=state_query(1);
    Bytes encoded;
    assert(inner_encode(123456,query.data(),query.size(),stamp,encoded));
    assert(encoded==reference);
    size_t total=0;
    assert(v2_packet_size(encoded.data(),6,total) && total==encoded.size());
    assert(v2_packet_matches_device(encoded.data(),encoded.size(),123456));
    assert(!v2_packet_matches_device(encoded.data(),encoded.size(),123457));
    Bytes frame;
    assert(inner_decode(encoded.data(),encoded.size(),frame) && frame==query);
    assert(!v2_packet_size(nullptr,0,total));
    for(size_t truncated=0;truncated<6;++truncated)
        assert(!v2_packet_size(encoded.data(),truncated,total));
    auto damaged=encoded;
    damaged[0]=0x83;
    assert(!v2_packet_size(damaged.data(),6,total));
    damaged=encoded;
    damaged[4]=0;
    damaged[5]=0;
    assert(!v2_packet_size(damaged.data(),6,total));
    damaged=encoded;
    damaged[4]=0xff;
    damaged[5]=0xff;
    assert(!v2_packet_size(damaged.data(),6,total));
    damaged=encoded;
    damaged.back()^=1;
    assert(!inner_decode(damaged.data(),damaged.size(),frame));
    // Existing V3 session packets MUST NOT be mistaken for V2.
    const uint8_t v3_header[6]{0x83,0x70,0,64,0x20,0x00};
    assert(!v2_packet_size(v3_header,6,total));

    const Bytes reply=state_reply_frame();
    assert(inner_encode(123456,reply.data(),reply.size(),stamp,encoded));
    assert(v2_packet_matches_device(encoded.data(),encoded.size(),123456));
    assert(inner_decode(encoded.data(),encoded.size(),frame));
    State state{};
    assert(parse_state(frame.data(),frame.size(),state));
    assert(state.power && state.mode==4 && std::fabs(state.setpoint_c-22.)<1e-6);
    assert(state.has_indoor && std::fabs(state.indoor_c-20.)<1e-6);
    assert(!state.has_outdoor);
    encoded[25]^=1; // Fake outer device ID: must be rejected.
    assert(!v2_packet_matches_device(encoded.data(),encoded.size(),123456));
    assert(!inner_decode(encoded.data(),encoded.size(),frame)); // Wrong MD5.
    std::cout<<"Midea V2 direct 5A5A / AES / MD5 read-only vectors PASS\n";
}