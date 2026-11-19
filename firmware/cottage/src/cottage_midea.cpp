#ifndef COTTAGE_BENCH_MODE
#include "cottage_midea.hpp"
#include <WiFi.h>
#include <esp_system.h>
#include <algorithm>
#include <ctime>

namespace enevina::midea {
namespace {
constexpr uint16_t port=6444;
constexpr uint32_t read_timeout_ms=2000;
bool allowed_lan_address(const IPAddress& ip) {
    const uint8_t a=ip[0],b=ip[1];
    return a==10 || (a==172 && b>=16 && b<=31) ||
           (a==192 && b==168);
}
bool read_exact(WiFiClient& tcp,uint8_t* data,size_t n) {
    if(!data || n>1024)return false;
    // Bounded deadline per packet; do not wait indefinitely for a partial frame.
    const unsigned long started=millis();
    size_t got=0;
    while(got<n && millis()-started<read_timeout_ms) {
        const int available=tcp.available();
        if(available>0) {
            const int count=tcp.read(data+got,std::min<size_t>(n-got,available));
            if(count<1)return false;
            got+=static_cast<size_t>(count);
        } else {
            if(!tcp.connected())return false;
            delay(2);
        }
    }
    return got==n;
}
bool read_8370(WiFiClient& tcp,Bytes& packet) {
    uint8_t header[6]{};
    if(!read_exact(tcp,header,sizeof(header)))return false;
    size_t total=0;
    if(!packet_size(header,6,total))return false;
    packet.assign(total,0);
    std::copy(header,header+6,packet.begin());
    return read_exact(tcp,packet.data()+6,total-6);
}
bool send_bytes(WiFiClient& tcp,const Bytes& data) {
    return !data.empty() && tcp.write(data.data(),data.size())==data.size();
}
bool now_stamp(uint8_t out[8]) {
    const time_t t=time(nullptr);
    if(t<1700000000)return false;
    struct tm utc{};
    if(!gmtime_r(&t,&utc))return false;
    out[0]=0;out[1]=static_cast<uint8_t>(utc.tm_sec);
    out[2]=static_cast<uint8_t>(utc.tm_min);
    out[3]=static_cast<uint8_t>(utc.tm_hour);
    out[4]=static_cast<uint8_t>(utc.tm_mday);
    out[5]=static_cast<uint8_t>(utc.tm_mon+1);
    out[6]=static_cast<uint8_t>((utc.tm_year+1900)%100);
    out[7]=static_cast<uint8_t>((utc.tm_year+1900)/100);
    return true;
}
} // namespace
ReadStatus read_device(const char* host,const Credential& creds,State& reading) {
    if(!host)return ReadStatus::InvalidAddress;
    IPAddress ip;
    if(!ip.fromString(host) || !allowed_lan_address(ip))
        return ReadStatus::InvalidAddress;
    if(!creds.device_id)return ReadStatus::InvalidCredentials;
    uint8_t stamp[8]{};
    if(!now_stamp(stamp))return ReadStatus::ProtocolFailed;
    WiFiClient tcp;
    tcp.setTimeout(read_timeout_ms);
    if(!tcp.connect(ip,port,1100))return ReadStatus::ConnectionFailed;
    auto hello=handshake_request(creds,0);
    if(!send_bytes(tcp,hello)) {tcp.stop();return ReadStatus::ConnectionFailed;}
    Bytes packet;
    if(!read_8370(tcp,packet)) {tcp.stop();return ReadStatus::AuthenticationFailed;}
    std::array<uint8_t,32> session{};
    if(!handshake_response(packet.data(),packet.size(),creds,session)) {
        tcp.stop();return ReadStatus::AuthenticationFailed;
    }
    const Bytes query=state_query(1);
    Bytes inner;
    if(!inner_encode(creds.device_id,query.data(),query.size(),stamp,inner)) {
        tcp.stop();return ReadStatus::ProtocolFailed;
    }
    uint8_t padding[15]{};
    esp_fill_random(padding,sizeof(padding));
    Bytes request;
    if(!encrypted_request(inner.data(),inner.size(),session,1,
                          padding,sizeof(padding),request) ||
       !send_bytes(tcp,request)) {
        tcp.stop();return ReadStatus::ProtocolFailed;
    }
    // Some V3 units send auxiliary responses before the queried AC state.
    ReadStatus status=ReadStatus::UnsupportedState;
    for(unsigned attempt=0;attempt<3;++attempt) {
        if(!read_8370(tcp,packet)) {status=ReadStatus::ProtocolFailed;break;}
        Bytes v2,frame;
        if(!encrypted_response(packet.data(),packet.size(),session,v2) ||
           !inner_decode(v2.data(),v2.size(),frame)) {
            status=ReadStatus::ProtocolFailed;break;
        }
        State value{};
        if(parse_state(frame.data(),frame.size(),value)) {
            reading=value;status=ReadStatus::Ok;break;
        }
    }
    tcp.stop();
    return status;
}
} // namespace enevina::midea
#endif