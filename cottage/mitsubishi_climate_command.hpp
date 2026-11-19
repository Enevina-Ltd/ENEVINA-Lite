#pragma once
// Verified live ESPHome WebServer uses POST to /climate/<encoded name>/set.
// Only builds requests; contains no HTTP client or actuator enable flag.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace enevina::cottage {
constexpr const char* mitsubishi_fd25va_climate_path =
    "/climate/Mitsubishi%20Electric%20MSZ-FD25VA/set";
struct MitsubishiHttpRequest {
    bool valid=false;
    std::array<char,160> path{};
};
enum class MitsubishiMode { Heat, Off };
inline bool mitsubishi_valid_setpoint(double c) {
    return std::isfinite(c) && c>=16.0 && c<=26.0 &&
           std::fabs(c*2.0-std::round(c*2.0))<1e-7;
}
inline MitsubishiHttpRequest mitsubishi_mode_request(MitsubishiMode mode) {
    MitsubishiHttpRequest out{};
    const char* token=mode==MitsubishiMode::Heat?"HEAT":"OFF";
    const int n=std::snprintf(out.path.data(),out.path.size(),
        "%s?mode=%s",mitsubishi_fd25va_climate_path,token);
    out.valid=n>0&&static_cast<size_t>(n)<out.path.size();
    return out;
}
inline MitsubishiHttpRequest mitsubishi_setpoint_request(double setpoint_c) {
    MitsubishiHttpRequest out{};
    if(!mitsubishi_valid_setpoint(setpoint_c))return out;
    const int n=std::snprintf(out.path.data(),out.path.size(),
        "%s?target_temperature=%.1f",mitsubishi_fd25va_climate_path,
        setpoint_c);
    out.valid=n>0&&static_cast<size_t>(n)<out.path.size();
    return out;
}
// This only confirms the bridge-reported snapshot, NEVER a fresh UART poll.
inline bool mitsubishi_bridge_readback_matches(MitsubishiMode mode,
        double desired, bool bridge_fresh,
        const char* observed_mode, double observed_setpoint) {
    if(!bridge_fresh||!observed_mode)return false;
    if(std::strcmp(observed_mode,mode==MitsubishiMode::Heat?"HEAT":"OFF"))return false;
    if(mode==MitsubishiMode::Off)return true;
    return mitsubishi_valid_setpoint(desired) &&
       std::isfinite(observed_setpoint) &&
       std::fabs(desired-observed_setpoint)<1e-6;
}
}
