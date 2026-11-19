#pragma once
// Owner-selectable optimization profiles for the price-aware room controller
// (cottage/price_bootstrap.hpp). A profile never changes a comfort limit and
// never authorizes a physical command: it only decides how much of the
// owner's allowed temperature band and of the price signal the controller may
// use. Pure C++, host-tested; PumpAdapter/Supervisor still gate every write.
//
//  Comfort   - steadiness first: stays on the narrow +-0.5 C thermostat band,
//              reacts only to large price differences (2x the base threshold).
//  Balanced  - uses 40 % of the headroom above/below the target for cheap
//              pre-heat and expensive-slot coasting; base price threshold.
//  Saving    - uses 80 % of the allowed band, half the price threshold and a
//              4 h price window instead of 2 h.
// Balanced and Saving also act 0.25 / 0.5 C inside the owner's limits (see
// ProfileTuning::limit_margin_c): the limits are what the owner can rely on,
// the margin is the controller's reaction distance.
//
// Without an owner band (comfort_min/max unset) there is no headroom, so the
// shares are 0 and Balanced is exactly the behaviour before profiles existed.
#include "cottage/price_bootstrap.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enevina::cottage {
enum class OptimizationProfile : uint8_t { Comfort=0, Balanced=1, Saving=2 };

inline const char* profile_name(OptimizationProfile p) {
    switch(p) {
    case OptimizationProfile::Comfort:return "comfort";
    case OptimizationProfile::Balanced:return "balanced";
    case OptimizationProfile::Saving:return "saving";
    }
    return "balanced";
}
inline bool parse_profile(const char* text,OptimizationProfile& out) {
    if(!text) return false;
    if(!std::strcmp(text,"comfort")) out=OptimizationProfile::Comfort;
    else if(!std::strcmp(text,"balanced")) out=OptimizationProfile::Balanced;
    else if(!std::strcmp(text,"saving")) out=OptimizationProfile::Saving;
    else return false;
    return true;
}

struct ProfileTuning {
    double advantage_scale;   // x the owner's base price-advantage fraction
    double headroom_share;    // share of the band used for pre-heat / coasting
    size_t window_slots;      // 15 min slots compared against their median
    // The controller acts this far INSIDE the owner's limits. A pump that is
    // capacity-limited in a cold spell needs time to recover; acting only at
    // the limit itself would let the room overshoot it (seen in simulation).
    double limit_margin_c;
};
inline ProfileTuning profile_tuning(OptimizationProfile p) {
    switch(p) {
    case OptimizationProfile::Comfort:return {2.0,0.0,8,0.0};
    case OptimizationProfile::Balanced:return {1.0,0.4,8,0.25};
    case OptimizationProfile::Saving:return {0.5,0.8,16,0.75};
    }
    return {1.0,0.4,8,0.25};
}
constexpr double kComfortMinAllowedC=5.0;
constexpr double kComfortMaxAllowedC=26.0;

// 0 = unset, otherwise 5..26 C in 0.5 steps; both set must leave >= 1 C
// between them. The target must lie inside (checked at decision time because
// the target lives in the mode configuration).
inline bool valid_comfort_limits(double lo,double hi) {
    auto ok=[](double v){
        return v==0 || (std::isfinite(v) && v>=kComfortMinAllowedC &&
                        v<=kComfortMaxAllowedC &&
                        std::fabs(v*2.0-std::round(v*2.0))<1e-9);
    };
    if(!ok(lo)||!ok(hi)) return false;
    return lo==0 || hi==0 || hi-lo>=1.0;
}

// Fills the band/price fields of `in` for the chosen profile. `in.desired_room_c`
// must already be set. Returns the price window (slots) the caller should
// supply (8 minimum); false if the inputs are not usable (in is left as is).
inline bool apply_profile(BootstrapInput& in,OptimizationProfile profile,
                          double comfort_min_c,double comfort_max_c,
                          double base_advantage_fraction,size_t& window_slots) {
    if(!valid_comfort_limits(comfort_min_c,comfort_max_c) ||
       !std::isfinite(base_advantage_fraction) || base_advantage_fraction<0 ||
       base_advantage_fraction>1 || !std::isfinite(in.desired_room_c)) return false;
    const ProfileTuning t=profile_tuning(profile);
    in.min_price_advantage_fraction=std::min(1.0,base_advantage_fraction*t.advantage_scale);
    // A limit on the wrong side of the target cannot be a limit: ignore it
    // rather than invert the band (the narrow +-0.5 C band always applies).
    const double d=in.desired_room_c;
    const double lo=(comfort_min_c>0 && comfort_min_c<=d-0.5)?comfort_min_c:0;
    const double hi=(comfort_max_c>0 && comfort_max_c>=d+0.5)?comfort_max_c:0;
    // Margin keeps the working limits at least 0.5 C from the target so the
    // narrow thermostat band always remains valid.
    const double work_lo=lo>0?std::min(lo+t.limit_margin_c,d-0.5):0;
    const double work_hi=hi>0?std::max(hi-t.limit_margin_c,d+0.5):0;
    in.comfort_lower_c=work_lo;
    in.comfort_upper_c=work_hi;
    in.preheat_c=work_hi>0?t.headroom_share*(work_hi-d):0;
    in.coast_c=work_lo>0?t.headroom_share*(d-work_lo):0;
    window_slots=t.window_slots;
    return true;
}
} // namespace enevina::cottage
