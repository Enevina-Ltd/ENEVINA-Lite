#pragma once
// Native AC B5 capability discovery. Protocol semantics follow the MIT-licensed
// midea-local and msmart-ng references; full attribution is in the .cpp file.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace enevina::midea {

enum class AcFeature : uint8_t {
    AutoMode, CoolMode, DryMode, HeatMode, SwingVertical, SwingHorizontal,
    FanSilent, FanLow, FanMedium, FanHigh, FanAuto, FanCustom,
    Eco, TurboCool, TurboHeat, FreezeProtection, AuxHeat, Display, Anion,
    Humidity, HalfDegree, VerticalAngle, HorizontalAngle, RateSelect,
    RateSelect2, RateSelect5, Sound, EnergyStats, EnergySetting, EnergyBcd,
    SelfClean, Breezeless, IndirectWind, Sleep, FreshAir
};

struct AcTemperatureRange {
    bool known=false;
    double minimum_c=16.0, maximum_c=30.0;
};

struct AcCapabilities {
    // A known bit distinguishes explicit unsupported from an omitted tag.
    uint64_t known=0, supported=0;
    bool received=false;
    // Indexed by wire mode: auto=1, cool=2, dry=3, heat=4, fan=5.
    std::array<AcTemperatureRange,6> temperatures{};
    bool reports(AcFeature feature) const;
    bool supports(AcFeature feature,bool fallback=false) const;
};

std::vector<uint8_t> capabilities_query(uint8_t message_id,
                                        bool additional=false,
                                        uint8_t protocol_version=0);
// Accept a complete verified AA/AC B5 response and merge the reported fields.
// A malformed response leaves every field in out unchanged. B5 flags never
// describe a feature's current on/off state. Duplicate nonempty tags: last wins.
bool parse_capabilities(const uint8_t* frame,size_t n,AcCapabilities& out);
double temperature_step(const AcCapabilities& caps);
AcTemperatureRange temperature_range(const AcCapabilities& caps,uint8_t mode);
bool mode_supported(const AcCapabilities& caps,uint8_t mode);
bool fan_supported(const AcCapabilities& caps,uint8_t speed);
bool swing_supported(const AcCapabilities& caps,uint8_t swing);

} // namespace enevina::midea
