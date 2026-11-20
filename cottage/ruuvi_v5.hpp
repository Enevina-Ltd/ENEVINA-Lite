#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace enevina::cottage {
// Ruuvi RAWv2, Data Format 5, complete BLE manufacturer data with
// little-endian 0x0499 manufacturer ID and 24-byte big-endian payload.
struct RuuviV5 {
    std::array<uint8_t,6> mac{};
    double temperature_c=0;
    double relative_humidity_pct=0;
    double pressure_pa=0;
    double battery_v=0;
    int tx_power_dbm=0;
    int rssi_dbm=0;
    uint8_t movement_count=0;
    uint16_t sequence=0;
};
// Shared rate-of-change acceptance for both the actual control binding and
// the nearby-sensor registry. A rejected BLE frame may update signal/last-heard
// metadata but must not refresh the temperature or humidity shown as measured.
inline bool ruuvi_plausible_progress(const RuuviV5& previous,
                                     const RuuviV5& next,uint64_t elapsed_ms) {
    if(elapsed_ms<300000 &&
       std::fabs(next.temperature_c-previous.temperature_c)>5.0)return false;
    if(elapsed_ms<60000 &&
       std::fabs(next.relative_humidity_pct-previous.relative_humidity_pct)>20.0)return false;
    return true;
}
bool decode_ruuvi_v5(const uint8_t* data,size_t len,int rssi,RuuviV5& result);
bool parse_ruuvi_mac(const char* text,std::array<uint8_t,6>& out);
enum class RuuviAccept : uint8_t {
    Accepted, Unbound, DifferentSensor, Invalid, Duplicate, OldSequence, Implausible
};
class RuuviBinding {
public:
    void bind(const std::array<uint8_t,6>& mac);
    void unbind();
    bool bound()const {return bound_;}
    const std::array<uint8_t,6>& mac()const {return bound_mac_;}
    RuuviAccept accept(const RuuviV5& sample,uint64_t monotonic_ms);
    bool valid(uint64_t monotonic_ms,uint64_t max_age_ms=600000)const;
    uint64_t age_ms(uint64_t monotonic_ms)const;
    const RuuviV5& latest()const {return latest_;}
private:
    std::array<uint8_t,6> bound_mac_{};
    RuuviV5 latest_{};
    uint64_t accepted_ms_=0;
    // Candidate restart sequence (untrusted until a second advancing frame).
    uint64_t restart_candidate_ms_=0;
    uint16_t restart_candidate_sequence_=0;
    bool bound_=false,has_sample_=false,restart_candidate_=false;
};
// Legacy NVS hourly_v1 has no sensor ID in the payload: it may be restored
// only when the configured sensor identity was already durably stored and
// matches the binding selected for THIS boot. Compile-time fallback alone
// is not proof of the old checkpoint's origin.
inline bool verified_history_origin(bool persisted_mac_valid,
                                    const std::array<uint8_t,6>& persisted_mac,
                                    const RuuviBinding& active) {
    return persisted_mac_valid && active.bound() &&
           persisted_mac==active.mac();
}
} // namespace enevina::cottage
