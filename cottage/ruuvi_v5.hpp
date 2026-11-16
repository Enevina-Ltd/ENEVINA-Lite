#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

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
    bool bound_=false,has_sample_=false;
};
} // namespace enevina::cottage
