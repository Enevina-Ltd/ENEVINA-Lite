#pragma once
#include "WiFi.h"
namespace midea_test { inline Bytes discovery; }
class WiFiUDP {
    IPAddress target_{};
public:
    bool begin(uint16_t){return true;}
    bool beginPacket(const IPAddress& target,uint16_t){target_=target;return true;}
    size_t write(const uint8_t*,size_t n){return n;}
    bool endPacket(){return true;}
    int parsePacket(){return static_cast<int>(midea_test::discovery.size());}
    int read(uint8_t* out,size_t n){n=std::min(n,midea_test::discovery.size());
        std::copy_n(midea_test::discovery.begin(),n,out);midea_test::discovery.clear();return int(n);}
    IPAddress remoteIP()const{return target_;}
    void flush(){midea_test::discovery.clear();}
    void stop(){}
};
