#pragma once
// Midea V2/V3 read-only UDP discovery shortlist. No auth, control or NVS write.
// Discovery replies are UNTRUSTED. Only a fresh authenticated V3/V2 status
// read may confirm a selected device for control (separate explicit approval).
// Discovery request: standard Midea LAN datagram, see msmart-ng MIT notice.
#include <array>
#include <cstddef>
#include <cstdint>

namespace enevina::midea {
inline constexpr std::array<uint8_t,72> discovery_request={
    0x5a,0x5a,0x01,0x11,0x48,0x00,0x92,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x7f,0x75,0xbd,0x6b,0x3e,0x4f,0x8b,0x76,
    0x2e,0x84,0x9c,0x6e,0x57,0x8d,0x65,0x90,0x03,0x6e,0x9d,0x43,
    0x42,0xa5,0x0f,0x1f,0x56,0x9e,0xb8,0xec,0x91,0x8e,0x92,0xe5};
struct DiscoveredDevice {
    uint8_t protocol=0;
    uint64_t id=0;
};
inline bool parse_discovery_reply(const uint8_t* packet,size_t length,
                                  DiscoveredDevice& out) {
    out={};
    if(!packet || length<72 || length>1024)return false;
    size_t pos=0;
    uint8_t protocol=0;
    if(packet[0]==0x83 && packet[1]==0x70) {
        // V3: 8-byte 8370 header, embedded V2-style packet, 16-byte tag.
        if(length<8+72+16)return false;
        pos=8;
        protocol=3;
    } else if(packet[0]==0x5a && packet[1]==0x5a)protocol=2;
    else return false;
    if(length-pos<72 || packet[pos]!=0x5a || packet[pos+1]!=0x5a)return false;
    // The six-byte little-endian ID precedes the AES-encrypted device info.
    // It is NOT an authentication proof; never use it to authorize writes.
    uint64_t id=0;
    for(size_t i=0;i<6;i++)
        id|=static_cast<uint64_t>(packet[pos+20+i])<<(8*i);
    if(!id)return false;
    out.protocol=protocol;
    out.id=id;
    return true;
}
} // namespace enevina::midea
