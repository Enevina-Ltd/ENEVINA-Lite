#pragma once
#include "pump/pump_adapter.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace enevina::cn105_uart {
constexpr uint8_t heat_pump_family=0x30;
constexpr uint8_t command_connect_reply=0x7a;
constexpr uint8_t command_data_reply=0x62;
constexpr size_t max_payload=16;
enum class Query : uint8_t { Settings=0x02, Room=0x03, Operating=0x06 };
enum class Mode : uint8_t { Heat, Dry, Cooling, Fan, Auto, Unknown };
enum class TemperatureEncoding : uint8_t { Unknown, WholeDegree, HalfDegree };
struct Settings {
    std::optional<bool> power;
    Mode mode=Mode::Unknown;
    std::optional<double> target_c;
    TemperatureEncoding encoding=TemperatureEncoding::Unknown;
};
struct Room {
    std::optional<double> indoor_c,outdoor_c;
};
struct Operation {
    std::optional<bool> running;
    std::optional<unsigned> compressor_hz;
};
struct Patch {
    std::optional<bool> power;
    std::optional<double> target_c;
    bool heat=false;
};
struct Frame {
    std::array<uint8_t,max_payload+6> bytes{};
    size_t size=0;
};
class FrameReader {
public:
    std::optional<Frame> append(uint8_t byte);
private:
    Frame partial_{};
};
Frame connect();
Frame query(Query);
bool response(const Frame&,uint8_t type);
std::optional<Settings> settings(const Frame&);
std::optional<Room> room(const Frame&);
std::optional<Operation> operation(const Frame&);
std::optional<Frame> heat_command(const Settings& before,const Patch&,const PumpHeatingLimits&);
bool readback_matches(const Settings& before,const Settings& after,const Patch&);
PumpRoomMode heating_mode(const Settings&);
}
