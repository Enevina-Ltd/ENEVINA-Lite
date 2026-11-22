#pragma once
#include "pump/pump_adapter.hpp"
#include <array>
#include <cstddef>
#include <optional>

namespace enevina::s21 {
constexpr uint8_t stx=2,etx=3,ack=6,nak=21;
constexpr size_t max_frame=32;
using Code=std::array<char,2>;
constexpr Code read_climate{'F','1'},climate_reply{'G','1'},write_climate{'D','1'};
constexpr Code read_room{'R','H'},room_reply{'S','H'};
constexpr Code read_outdoor{'R','a'},outdoor_reply{'S','a'};
enum class Mode : uint8_t { Auto=0,Dry=2,Cooling=3,Heat=4,Fan=6,Unknown=255 };
struct Frame {
    std::array<uint8_t,max_frame> bytes{};
    size_t size=0;
};
struct Climate {
    bool power=false;
    Mode mode=Mode::Unknown;
    std::optional<double> target_c;
    std::array<uint8_t,4> wire{};
};
struct Patch {
    std::optional<bool> power;
    std::optional<double> target_c;
    bool heat=false;
};
class Reader {
public:
    std::optional<Frame> append(uint8_t byte);
private:
    Frame frame_{};
};
Frame query(Code command);
bool response(const Frame& frame,Code command,size_t payload_size);
std::optional<Climate> climate(const Frame&);
std::optional<double> temperature(const Frame&,Code command);
std::optional<Frame> command(const Climate& before,const Patch&,const PumpHeatingLimits&);
bool readback_matches(const Climate& before,const Climate& after,const Patch&);
PumpRoomMode heating_mode(const Climate&);
}
