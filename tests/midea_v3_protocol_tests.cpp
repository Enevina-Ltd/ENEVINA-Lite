#include "pump/midea_v3_protocol.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
using namespace enevina::midea;
static Bytes hx(const char* s) {
    Bytes out(std::strlen(s)/2);
    assert(decode_hex(s,out.data(),out.size()));
    return out;
}
static std::string hex(const uint8_t* b,size_t n) {
    static const char* digits="0123456789abcdef";
    std::string s;
    for(size_t i=0;i<n;++i){s+=digits[b[i]>>4];s+=digits[b[i]&15];}
    return s;
}
int main() {
    Credential creds{};
    const std::string token(128,'a'),key(64,'b');
    assert(decode_credentials("123456",token.c_str(),key.c_str(),creds));
    assert(creds.device_id==123456);
    assert(!decode_credentials("0",token.c_str(),key.c_str(),creds));
    assert(!decode_credentials("-1",token.c_str(),key.c_str(),creds));
    assert(!decode_credentials("123456",token.c_str(),"bad",creds));
    assert(!decode_credentials("18446744073709551616",token.c_str(),key.c_str(),creds));
    auto query=state_query(1);
    assert(hex(query.data(),query.size())==
      "aa21ac00000000000003418100ff03ff0002000000000000000000000000030169fe");

    // Protocol reference: msmart-ng 2026.8.1, packet UTC=2026-11-01 02:03:04,
    // device ID 123456, deterministic message ID and payload.
    const uint8_t stamp[8]{0,4,3,2,1,11,26,20};
    Bytes actual;
    assert(inner_encode(123456,query.data(),query.size(),stamp,actual));
    auto reference=hx("5a5a0111680020000000000000040302010b1a1440e201000000000000000000000000000000000065cd8728fc6a5d31e6464235e81fa3b3e10c552981b23022cb71ea0fc54dc25e29ff4bed361a3a2fbe593a9d1a721035eba6281bfc401cb00b6c2e5b18452ebd");
    assert(actual==reference);
    Bytes frame;
    assert(inner_decode(actual.data(),actual.size(),frame));
    assert(frame==query);
    actual.back()^=1;
    assert(!inner_decode(actual.data(),actual.size(),frame));

    Credential c{};
    c.device_id=123456;
    for(size_t i=0;i<64;++i)c.token[i]=static_cast<uint8_t>(i);
    for(size_t i=0;i<32;++i)c.key[i]=static_cast<uint8_t>(i+32);
    auto hello=handshake_request(c,3);
    assert(hello.size()==72 && hello[0]==0x83 && hello[5]==0 && hello[7]==3);
    const auto handshake=hx("8370004020010000b1e02cf80070f546cf0c6c0a703fe93ccee1034c3ad001f7c9144d863cd92cc902d449a31fbb267c8f352e9968a79e3e5fc95c1bbeaa502fd6454ebde5a4bedc");
    std::array<uint8_t,32> session{};
    assert(handshake_response(handshake.data(),handshake.size(),c,session));
    for(size_t i=0;i<32;++i)assert(session[i]==(uint8_t(0x11)^c.key[i]));
    auto bad=handshake;
    bad.back()^=1;
    assert(!handshake_response(bad.data(),bad.size(),c,session));

    // 8370 response golden vector from independent AES-CBC/SHA256 reference.
    const auto resp=hx("8370008e2063fe7a8ad9e1572e08fa55109a623f733b0739c24feb13e7946659d5de99f148f993fb8b954aeda19a1b9e34d3c07305ba4a6bd87acd374923d8dd186102206809dbfb3cb8b6df8ae3013324e0e1a193070027bbbbb8ed4ab26bd70f3ad6fcf2041a39158d3d95e295abfe27ee507db384c4229b40a0f519824553a88ae27e37d9bf17e54252729146ffc6d5f5583089c3");
    std::array<uint8_t,32> key0{};
    for(size_t i=0;i<32;++i)key0[i]=static_cast<uint8_t>(i);
    Bytes extracted;
    assert(encrypted_response(resp.data(),resp.size(),key0,extracted));
    assert(extracted==reference);
    bad=resp;bad.back()^=1;
    assert(!encrypted_response(bad.data(),bad.size(),key0,extracted));
    bad=resp;bad[4]^=1;
    assert(!encrypted_response(bad.data(),bad.size(),key0,extracted));
    uint8_t padding[15]{}; for(uint8_t i=0;i<15;++i)padding[i]=0x42;
    Bytes encrypted;
    assert(encrypted_request(reference.data(),reference.size(),key0,3,padding,15,encrypted));
    assert(encrypted.size()==resp.size());
    assert(encrypted[5]==0x66); // REQUEST type, same pad value as response.
    assert(!encrypted_request(reference.data(),reference.size(),key0,3,padding,0,encrypted));

    // Synthetic AC C0 response: 22 body bytes + CRC + frame checksum.
    Bytes p(22,0);p[0]=0xc0;p[1]=1;p[2]=uint8_t(4u<<5 | 6u);
    p[3]=102;p[7]=1;p[11]=90;p[12]=0xff;p[15]=0;
    uint8_t crc=0;
    for(auto b:p){crc^=b;for(int i=0;i<8;++i)crc=(crc&1)?uint8_t((crc>>1)^0x8c):uint8_t(crc>>1);}
    p.push_back(crc);
    Bytes f(10,0);f[0]=0xaa;f[1]=static_cast<uint8_t>(10+p.size());f[2]=0xac;f[9]=3;
    f.insert(f.end(),p.begin(),p.end());
    uint8_t cs=0;for(size_t i=1;i<f.size();++i)cs=static_cast<uint8_t>(cs+f[i]);
    f.push_back(static_cast<uint8_t>(-cs));
    State state{};
    assert(parse_state(f.data(),f.size(),state));
    assert(state.power && state.mode==4 && state.setpoint_c==22);
    assert(state.has_indoor && std::fabs(state.indoor_c-20)<0.001);
    assert(!state.has_outdoor);
    assert(state.freeze_protection_known);
    assert(!state.independent_aux_heat);
    f[f.size()-2]^=1;
    assert(!parse_state(f.data(),f.size(),state));
    // C0 packets may omit optional humidity and freeze-protection fields.
    // Test 17- and 18-byte bodies, both supported checksum styles, and 16
    // bytes (below minimum). These regress the previous 19-byte guard.
    for(size_t body_length: {size_t(16),size_t(17),size_t(18),size_t(19)}) {
        for(bool use_crc: {false,true}) {
            Bytes short_body(p.begin(),p.begin()+body_length);
            uint8_t payload_crc=0;
            for(uint8_t v:short_body) {
                payload_crc^=v;
                for(int k=0;k<8;++k)
                    payload_crc=(payload_crc&1)
                        ?uint8_t((payload_crc>>1)^0x8c)
                        :uint8_t(payload_crc>>1);
            }
            uint8_t payload_sum=0;
            for(uint8_t v:short_body)
                payload_sum=static_cast<uint8_t>(payload_sum+v);
            short_body.push_back(use_crc?payload_crc:static_cast<uint8_t>(-payload_sum));
            Bytes short_frame(10,0);
            short_frame[0]=0xaa;
            short_frame[1]=static_cast<uint8_t>(10+short_body.size());
            short_frame[2]=0xac;
            short_frame[9]=3;
            short_frame.insert(short_frame.end(),short_body.begin(),short_body.end());
            uint8_t total_sum=0;
            for(size_t k=1;k<short_frame.size();++k)
                total_sum=static_cast<uint8_t>(total_sum+short_frame[k]);
            short_frame.push_back(static_cast<uint8_t>(-total_sum));
            assert(parse_state(short_frame.data(),short_frame.size(),state)==
                   (body_length>=17));
            if(body_length>=17) {
                assert(!state.freeze_protection_known);
                ControlPatch change{};
                change.changed=ControlTemperature;
                change.setpoint_c=22.0;
                Bytes unsafe{};
                assert(!set_state_command(state,change,1,unsafe));
            }
            short_frame.back()^=1;
            assert(!parse_state(short_frame.data(),short_frame.size(),state));
        }
    }
    std::cout<<"Midea V3 test vectors and negative checks PASS\n";
}