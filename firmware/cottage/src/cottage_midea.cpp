#ifndef COTTAGE_BENCH_MODE
#include "cottage_midea.hpp"
#include "cottage_wifi.hpp"
#include <WiFi.h>
#include <esp_system.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>

namespace enevina::midea {
namespace {
constexpr uint16_t port=6444;
std::atomic<uint8_t> last_v3_read_detail_code{0};
std::atomic<uint8_t> last_v3_reply_opcode_code{0};
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
bool read_5a5a(WiFiClient& tcp,Bytes& packet) {
    uint8_t header[6]{};
    if(!read_exact(tcp,header,sizeof(header)))return false;
    size_t total=0;
    if(!v2_packet_size(header,6,total))return false;
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
ReadStatus read_device_v2(const char* host,uint64_t device_id,State& reading) {
    if(!host)return ReadStatus::InvalidAddress;
    IPAddress ip;
    if(!ip.fromString(host) || !allowed_lan_address(ip))
        return ReadStatus::InvalidAddress;
    if(!device_id)return ReadStatus::InvalidCredentials;
    uint8_t stamp[8]{};
    if(!now_stamp(stamp))return ReadStatus::TimeNotReady;
    const Bytes query=state_query(1);
    Bytes request;
    // V2 is a directly signed/encrypted 5A5A packet. It does not use the
    // 8370 hello, session key or any cloud token/key.
    if(!inner_encode(device_id,query.data(),query.size(),stamp,request))
        return ReadStatus::ProtocolFailed;
    WiFiClient tcp;
    tcp.setTimeout(read_timeout_ms);
    if(!tcp.connect(ip,port,1100))return ReadStatus::ConnectionFailed;
    if(!send_bytes(tcp,request)) {tcp.stop();return ReadStatus::ConnectionFailed;}
    ReadStatus result=ReadStatus::UnsupportedState;
    Bytes packet,frame;
    for(unsigned attempt=0;attempt<3;++attempt) {
        if(!read_5a5a(tcp,packet)) {result=ReadStatus::ProtocolFailed;break;}
        if(!v2_packet_matches_device(packet.data(),packet.size(),device_id) ||
           !inner_decode(packet.data(),packet.size(),frame)) {
            result=ReadStatus::ProtocolFailed;break;
        }
        State value{};
        if(parse_state(frame.data(),frame.size(),value)) {
            reading=value;
            result=ReadStatus::Ok;
            break;
        }
    }
    tcp.stop();
    return result;
}
uint8_t last_v3_read_detail(){return last_v3_read_detail_code.load();}
uint8_t last_v3_reply_opcode(){return last_v3_reply_opcode_code.load();}
ReadStatus read_device(const char* host,const Credential& creds,State& reading) {
    last_v3_read_detail_code.store(0);
    last_v3_reply_opcode_code.store(0);
    if(!host)return ReadStatus::InvalidAddress;
    IPAddress ip;
    if(!ip.fromString(host) || !allowed_lan_address(ip))
        return ReadStatus::InvalidAddress;
    if(!creds.device_id)return ReadStatus::InvalidCredentials;
    uint8_t stamp[8]{};
    if(!now_stamp(stamp))return ReadStatus::TimeNotReady;
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
    // Match msmart-ng V3's post-authentication stabilization interval:
    // some modules cannot process the first status query immediately.
    vTaskDelay(pdMS_TO_TICKS(1100));
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
        if(!encrypted_response(packet.data(),packet.size(),session,v2)) {
            last_v3_read_detail_code.store(20);
            status=ReadStatus::ProtocolFailed;break;
        }
        if(!inner_decode(v2.data(),v2.size(),frame)) {
            last_v3_read_detail_code.store(21);
            status=ReadStatus::ProtocolFailed;break;
        }
        if(frame.size()>10)
            last_v3_reply_opcode_code.store(frame[10]);
        State value{};
        if(parse_state(frame.data(),frame.size(),value)) {
            last_v3_read_detail_code.store(0);
            reading=value;status=ReadStatus::Ok;break;
        }
        last_v3_read_detail_code.store(
            parse_state_failure_code(frame.data(),frame.size()));
    }
    tcp.stop();
    return status;
}
// Source of truth is a fresh authenticated status query, not the last
// dashboard sample. Do not trust a mere TCP write as an applied command.
ReadStatus write_device_v2(const char* host,uint64_t id,
                           const ControlPatch& patch,State& confirmed,
                           uint32_t expected_wifi_generation,
                           double approved_min_c,double approved_max_c) {
    if(!valid_control_patch(patch))return ReadStatus::ProtocolFailed;
    if(WiFi.status()!=WL_CONNECTED ||
       cottage_wifi_link_generation()!=expected_wifi_generation)
       return ReadStatus::ResourceBusy;
    State original{};
    const ReadStatus initial=read_device_v2(host,id,original);
    if(initial!=ReadStatus::Ok)return initial;
    const double setpoint=(patch.changed&ControlTemperature)?
                          patch.setpoint_c:original.setpoint_c;
    if(!std::isfinite(setpoint) ||
       setpoint<approved_min_c || setpoint>approved_max_c)
        return ReadStatus::UnsupportedState;
    if(WiFi.status()!=WL_CONNECTED ||
       cottage_wifi_link_generation()!=expected_wifi_generation)
       return ReadStatus::ResourceBusy;
    Bytes control;
    if(!set_state_command(original,patch,2,control))
        return ReadStatus::UnsupportedState;
    uint8_t stamp[8]{};
    if(!now_stamp(stamp))return ReadStatus::TimeNotReady;
    Bytes request;
    if(!inner_encode(id,control.data(),control.size(),stamp,request))
        return ReadStatus::ProtocolFailed;
    IPAddress ip;
    if(!host || !ip.fromString(host) || !allowed_lan_address(ip))
        return ReadStatus::InvalidAddress;
    WiFiClient tcp;
    tcp.setTimeout(read_timeout_ms);
    if(!tcp.connect(ip,port,1100))return ReadStatus::ConnectionFailed;
    if(WiFi.status()!=WL_CONNECTED ||
       cottage_wifi_link_generation()!=expected_wifi_generation) {
        tcp.stop();
        return ReadStatus::ResourceBusy;
    }
    if(!send_bytes(tcp,request)) {
        tcp.stop();
        return ReadStatus::ConnectionFailed;
    }
    // A response may contain a C0 state, a short ACK or an auxiliary frame.
    // Drain a bounded number of validated packets. Never send a second SET.
    for(unsigned attempt=0;attempt<3;++attempt) {
        Bytes packet,inner;
        if(!read_5a5a(tcp,packet))break;
        if(!v2_packet_matches_device(packet.data(),packet.size(),id) ||
           !inner_decode(packet.data(),packet.size(),inner))break;
    }
    tcp.stop();
    State fresh{};
    const ReadStatus checked=read_device_v2(host,id,fresh);
    if(cottage_wifi_link_generation()!=expected_wifi_generation ||
       checked!=ReadStatus::Ok ||
       !control_patch_observed(fresh,patch) ||
       !preserved_state_observed(original,fresh,patch))
        return ReadStatus::ControlNotConfirmed;
    confirmed=fresh;
    return ReadStatus::Ok;
}
ReadStatus write_device(const char* host,const Credential& creds,
                        const ControlPatch& patch,State& confirmed,
                           uint32_t expected_wifi_generation,
                           double approved_min_c,double approved_max_c) {
    if(!valid_control_patch(patch))return ReadStatus::ProtocolFailed;
    if(WiFi.status()!=WL_CONNECTED ||
       cottage_wifi_link_generation()!=expected_wifi_generation)
       return ReadStatus::ResourceBusy;
    State original{};
    const ReadStatus initial=read_device(host,creds,original);
    if(initial!=ReadStatus::Ok)return initial;
    const double setpoint=(patch.changed&ControlTemperature)?
                          patch.setpoint_c:original.setpoint_c;
    if(!std::isfinite(setpoint) ||
       setpoint<approved_min_c || setpoint>approved_max_c)
        return ReadStatus::UnsupportedState;
    if(WiFi.status()!=WL_CONNECTED ||
       cottage_wifi_link_generation()!=expected_wifi_generation)
       return ReadStatus::ResourceBusy;
    Bytes control;
    if(!set_state_command(original,patch,2,control))
        return ReadStatus::UnsupportedState;
    uint8_t stamp[8]{};
    if(!now_stamp(stamp))return ReadStatus::TimeNotReady;
    Bytes inner;
    if(!inner_encode(creds.device_id,control.data(),control.size(),stamp,inner))
        return ReadStatus::ProtocolFailed;
    IPAddress ip;
    if(!host || !ip.fromString(host) || !allowed_lan_address(ip))
        return ReadStatus::InvalidAddress;
    WiFiClient tcp;
    tcp.setTimeout(read_timeout_ms);
    if(!tcp.connect(ip,port,1100))return ReadStatus::ConnectionFailed;
    if(!send_bytes(tcp,handshake_request(creds,0))) {
        tcp.stop();return ReadStatus::ConnectionFailed;
    }
    Bytes packet;
    if(!read_8370(tcp,packet)) {
        tcp.stop();return ReadStatus::AuthenticationFailed;
    }
    std::array<uint8_t,32> session{};
    if(!handshake_response(packet.data(),packet.size(),creds,session)) {
        tcp.stop();return ReadStatus::AuthenticationFailed;
    }
    // V3 device needs the same post-auth stabilization for writes as reads.
    vTaskDelay(pdMS_TO_TICKS(1100));
    uint8_t padding[15]{};
    esp_fill_random(padding,sizeof(padding));
    Bytes request;
    if(!encrypted_request(inner.data(),inner.size(),session,1,
                          padding,sizeof(padding),request)) {
        tcp.stop();return ReadStatus::ProtocolFailed;
    }
    if(WiFi.status()!=WL_CONNECTED ||
       cottage_wifi_link_generation()!=expected_wifi_generation) {
        tcp.stop();return ReadStatus::ResourceBusy;
    }
    if(!send_bytes(tcp,request)) {
        tcp.stop();return ReadStatus::ConnectionFailed;
    }
    // No unsafe retry on ACK loss. Validate any responses, then use
    // independent authenticated readback as the commit evidence.
    for(unsigned attempt=0;attempt<3;++attempt) {
        if(!read_8370(tcp,packet))break;
        Bytes received,decoded;
        if(!encrypted_response(packet.data(),packet.size(),session,received) ||
           !inner_decode(received.data(),received.size(),decoded))break;
    }
    tcp.stop();
    State fresh{};
    const ReadStatus checked=read_device(host,creds,fresh);
    if(cottage_wifi_link_generation()!=expected_wifi_generation ||
       checked!=ReadStatus::Ok ||
       !control_patch_observed(fresh,patch) ||
       !preserved_state_observed(original,fresh,patch))
        return ReadStatus::ControlNotConfirmed;
    confirmed=fresh;
    return ReadStatus::Ok;
}
} // namespace enevina::midea
#endif