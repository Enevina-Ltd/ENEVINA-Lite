#include "cottage/ruuvi.hpp"
#include <cmath>
namespace enevina::cottage {
bool decode_ruuvi_rawv5_temperature(const uint8_t* b,size_t n,double& out) {
    // 0x0499 in LE, data format 0x05, signed 16-bit in units 0.005 C.
    if(!b || n<5 || b[0]!=0x99 || b[1]!=0x04 || b[2]!=0x05) return false;
    const uint16_t raw=(static_cast<uint16_t>(b[3])<<8)|b[4];
    if(raw==0x8000) return false; // sensor unavailable sentinel
    const auto signed_raw=static_cast<int16_t>(raw);
    const double value=signed_raw*0.005;
    if(!std::isfinite(value)||value<-40||value>80) return false;
    out=value;
    return true;
}
}
