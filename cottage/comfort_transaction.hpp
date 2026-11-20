#pragma once
// Durable, single-record intent for wizard Comfort settings spanning the
// independent "site" and "cottage_air" NVS namespaces. Stored BEFORE the
// site transaction; on reboot the air service completes it or fails closed.
// This record is internal and does not modify the shipped v4 settings layout.
#include "cottage/site_settings.hpp"
#include "cottage/air_humidity.hpp"
#include <cmath>
#include <cstdint>

namespace enevina::cottage {
struct ComfortTxn {
    uint32_t magic=0x43545831;     // CTX1
    uint16_t version=1;
    uint8_t mode=2;               // Hold or Occupied only
    uint8_t profile=1;            // owner-selected profile
    float target=12;
    float min_c=0,max_c=0;
};
inline bool valid_comfort_txn(const ComfortTxn& t) {
    if(t.magic!=0x43545831 || t.version!=1 || t.mode<2 || t.mode>3 ||
       t.profile>2 || !std::isfinite(t.target) ||
       !valid_comfort_limits(t.min_c,t.max_c))return false;
    return t.mode==2 ?
        t.target>=5&&t.target<=24 :
        t.target>=16&&t.target<=26&&
        std::fabs(t.target*2-std::round(t.target*2))<1e-5;
}
inline bool comfort_site_matches(const ComfortTxn& t,const SiteSettings& s) {
    return valid_comfort_txn(t) && s.setup_step>=6 &&
        static_cast<uint8_t>(s.profile)==t.profile &&
        std::fabs(s.comfort_min_c-t.min_c)<0.001 &&
        std::fabs(s.comfort_max_c-t.max_c)<0.001;
}
inline bool comfort_mode_matches(const ComfortTxn& t,const CottageModeConfig& c) {
    if(!valid_comfort_txn(t) || static_cast<uint8_t>(c.mode)!=t.mode)return false;
    const double current=t.mode==3?c.occupied_target_c:c.hold_target_c;
    return std::fabs(current-t.target)<0.001;
}
inline void comfort_apply_target(const ComfortTxn& t,CottageModeConfig& c) {
    if(t.mode==3)c.occupied_target_c=t.target;
    else c.hold_target_c=t.target;
}
}
