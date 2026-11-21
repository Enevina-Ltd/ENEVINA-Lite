#pragma once
#include "Arduino.h"
#include "IPAddress.h"
#include <algorithm>
#include <functional>
#include <vector>
constexpr int WL_CONNECTED=3;
namespace midea_test {
using Bytes=std::vector<uint8_t>;
inline std::function<Bytes(const Bytes&)> server;
inline bool connected=true,short_write=false;
inline unsigned connection_count=0;
inline std::vector<Bytes> writes;
}
struct FakeWiFi {int status()const{return midea_test::connected?WL_CONNECTED:0;}};
inline FakeWiFi WiFi;
class WiFiClient {
    bool open_=false;
    midea_test::Bytes receive_;
public:
    void setTimeout(unsigned long){}
    bool connect(const IPAddress&,uint16_t,unsigned long) {
        ++midea_test::connection_count;open_=true;return true;
    }
    bool connected()const{return open_;}
    int available()const{return static_cast<int>(receive_.size());}
    int read(uint8_t* out,size_t n) {
        n=std::min(n,receive_.size());
        std::copy_n(receive_.begin(),n,out);receive_.erase(receive_.begin(),receive_.begin()+n);
        return static_cast<int>(n);
    }
    size_t write(const uint8_t* data,size_t n) {
        midea_test::Bytes request(data,data+n);midea_test::writes.push_back(request);
        if(midea_test::server) {
            auto response=midea_test::server(request);
            receive_.insert(receive_.end(),response.begin(),response.end());
        }
        return midea_test::short_write && n ? n-1:n;
    }
    void stop(){open_=false;receive_.clear();}
};
