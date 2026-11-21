#pragma once
#include <array>
#include <cstdio>
#include <cstdint>
class IPAddress {
    std::array<uint8_t,4> bytes_{};
public:
    IPAddress()=default;
    IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d):bytes_{a,b,c,d}{}
    bool fromString(const char* s) {
        unsigned a,b,c,d;char trailing;
        if(!s || std::sscanf(s,"%u.%u.%u.%u%c",&a,&b,&c,&d,&trailing)!=4 ||
           a>255 || b>255 || c>255 || d>255)return false;
        bytes_={uint8_t(a),uint8_t(b),uint8_t(c),uint8_t(d)};return true;
    }
    uint8_t operator[](size_t i)const{return bytes_[i];}
    explicit operator uint32_t()const{return uint32_t(bytes_[0])<<24|
        uint32_t(bytes_[1])<<16|uint32_t(bytes_[2])<<8|bytes_[3];}
    bool operator==(const IPAddress& other)const{return bytes_==other.bytes_;}
    bool operator!=(const IPAddress& other)const{return !(*this==other);}
};
