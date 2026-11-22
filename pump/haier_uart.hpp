#pragma once

#include "pump/pump_adapter.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace enevina::haier {
enum class Family { Hon, SmartAir2 };
enum class Mode : uint8_t { Unknown=0, Auto=1, Cool=2, Dry=3, Heat=4, Fan=5, Other=6 };
enum class Type : uint8_t {
    Control=1, Status=2, Invalid=3, Version=0x61, VersionReply=0x62,
    DeviceId=0x70, DeviceIdReply=0x71
};
constexpr uint16_t read_status=0x4d01, status_reply=0x6d01;
constexpr size_t max_payload=233;

struct Frame {
    Type type{};
    bool crc=false;
    std::array<uint8_t,max_payload> data{};
    size_t size=0;
    uint16_t subtype() const;
};
struct Packet {
    std::array<uint8_t,490> bytes{};
    size_t size=0;
};
class Reader {
public:
    std::optional<Frame> append(uint8_t byte);
private:
    std::array<uint8_t,max_payload+11> body_{};
    size_t used_=0;
    uint8_t separators_=0;
    bool escaped_=false;
    std::optional<Frame> finish();
    void reset();
};
std::optional<Packet> encode(Type type,bool crc,const uint8_t* data,size_t size);
Packet version_query();
Packet query(Type type,bool crc,uint16_t subtype=0);

struct Climate {
    Family family=Family::Hon;
    bool power=false, crc=false, cooling_only=false, protected_mode=false, fahrenheit=false;
    Mode mode=Mode::Unknown;
    std::optional<double> target_c, indoor_c, outdoor_c;
    std::array<uint8_t,24> wire{};
};
struct Patch {
    std::optional<bool> power;
    std::optional<bool> heat;
    std::optional<double> target_c;
};
// A real version reply differentiates the supported hOn layout from SmartAir2's invalid reply.
std::optional<bool> hon_crc(const Frame& version);
std::optional<Climate> climate(const Frame& reply,Family family,bool crc);
bool same_settings(const Climate& a,const Climate& b);
std::optional<Packet> command(const Climate& before,const Patch& patch,const PumpHeatingLimits& limits);
bool readback_matches(const Climate& before,const Climate& after,const Patch& patch);
} // namespace enevina::haier
