#include "pump/midea_discovery_info.hpp"
#include "pump/midea_v3_protocol.hpp"
#include <algorithm>

// Discovery payload layout from midea-local 15.2.1 (MIT), discover.py.
// Its upstream source and license are vendored under third_party/midea-local.
namespace enevina::midea {
bool parse_discovery_info(const uint8_t* packet,size_t length,DiscoveryInfo& out) {
    DiscoveryInfo candidate{};
    if(!parse_discovery_reply(packet,length,candidate.device))return false;
    const uint8_t* inner=packet;
    size_t inner_size=length;
    if(candidate.device.protocol==3) {
        size_t total=0;
        if(!packet_size(packet,length,total) || total!=length || length<96)return false;
        inner=packet+8;inner_size=length-24;
    }
    Bytes decoded;
    if(!v2_packet_matches_device(inner,inner_size,candidate.device.id) ||
       !inner_decode(inner,inner_size,decoded) || decoded.size()<42)return false;
    const size_t ssid_size=decoded[40];
    if(ssid_size<4 || ssid_size>decoded.size()-41)return false;
    const uint8_t* ssid=decoded.data()+41;
    const uint8_t* separator=std::find(ssid,ssid+ssid_size,static_cast<uint8_t>('_'));
    if(separator==ssid+ssid_size || ssid+ssid_size-separator<4 || separator[3]!='_')return false;
    auto nibble=[](uint8_t c)->int {
        if(c>='0' && c<='9')return c-'0';
        if(c>='a' && c<='f')return c-'a'+10;
        if(c>='A' && c<='F')return c-'A'+10;
        return -1;
    };
    const int a=nibble(separator[1]),b=nibble(separator[2]);
    if(a<0 || b<0)return false;
    candidate.device_type=static_cast<uint8_t>((a<<4)|b);
    const uint32_t port=static_cast<uint32_t>(decoded[4]) |
        (static_cast<uint32_t>(decoded[5])<<8) |
        (static_cast<uint32_t>(decoded[6])<<16) |
        (static_cast<uint32_t>(decoded[7])<<24);
    if(!port || port>65535)return false;
    candidate.port=static_cast<uint16_t>(port);
    for(size_t i=0;i<8;++i) {
        const char c=static_cast<char>(decoded[17+i]);
        // A malformed model must not become UI markup or a model exception.
        if(!((c>='0' && c<='9') || (c>='A' && c<='Z') ||
             (c>='a' && c<='z')))return false;
        candidate.model[i]=c;
    }
    const size_t subtype_offset=57+ssid_size;
    if(decoded.size()>=subtype_offset+2) {
        candidate.subtype_known=true;
        candidate.subtype=static_cast<uint16_t>(decoded[subtype_offset]) |
            (static_cast<uint16_t>(decoded[subtype_offset+1])<<8);
    }
    out=candidate;
    return true;
}
}
