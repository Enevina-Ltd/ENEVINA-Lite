#pragma once
#include <HTTPClient.h>
#include <WiFi.h>
#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace enevina::cottage {

inline bool local_http_peer(const char* address,IPAddress& ip) {
    if(!address || !ip.fromString(address))return false;
    const auto a=ip[0],b=ip[1];
    if(!(a==10 || (a==192 && b==168) || (a==172 && b>=16 && b<=31)))return false;
    const auto local=static_cast<uint32_t>(WiFi.localIP());
    const auto mask=static_cast<uint32_t>(WiFi.subnetMask());
    const auto peer=static_cast<uint32_t>(ip);
    const auto network=local&mask;
    return local && mask && (peer&mask)==network && peer!=local &&
        peer!=network && peer!=(network|~mask);
}

template<typename Current>
std::optional<std::string> bounded_http_body(HTTPClient& req,int status,size_t limit,Current current) {
    const int len=req.getSize();
    if(status!=200 || len==0 || len>static_cast<int>(limit) ||
       !req.header("Transfer-Encoding").isEmpty())return {};
    auto& stream=req.getStream();
    std::string body;
    std::array<uint8_t,128> bytes{};
    const uint32_t start=millis();
    bool complete=false;
    while(current() && millis()-start<1300) {
        if(len>=0 && body.size()==static_cast<size_t>(len)) {complete=true;break;}
        const int available=stream.available();
        if(available>0) {
            const auto count=static_cast<size_t>(available);
            const size_t residual=(len>=0?static_cast<size_t>(len):limit)-body.size();
            if(count>residual)break;
            const int read=stream.read(bytes.data(),std::min(count,bytes.size()));
            if(read>0)body.append(reinterpret_cast<const char*>(bytes.data()),read);
            else vTaskDelay(pdMS_TO_TICKS(15));
        } else if(!stream.connected()) {
            complete=len<0;
            break;
        } else vTaskDelay(pdMS_TO_TICKS(15));
    }
    if(!complete || body.empty() || !current())return {};
    return body;
}
}
