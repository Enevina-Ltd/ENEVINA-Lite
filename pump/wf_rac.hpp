#pragma once
#include "pump/pump_adapter.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace enevina::wf_rac {
constexpr size_t max_frame=256;
enum class Mode : uint8_t { Auto, Dry, Cooling, Fan, Heat, Unknown };
struct Reading {
    bool power=false;
    Mode mode=Mode::Unknown;
    uint8_t model_number=0;
    std::optional<double> target_c,indoor_c,outdoor_c;
    bool compressor_demand=false;
    bool self_clean=false;
    uint8_t fault=0;
};
struct Patch {
    std::optional<bool> power;
    std::optional<double> target_c;
    bool heat=false;
};
std::optional<std::string> unit_id(std::string_view text);
bool operator_id_valid(std::string_view text);
bool same_request(const Reading& a,const Reading& b);
bool changes_mode(const Reading& before,const Patch& patch);
bool permits_heat(const Reading& before,const Patch& patch);
uint16_t crc16(std::string_view bytes);
std::optional<Reading> decode(std::string_view bytes);
std::optional<std::string> command(const Patch& patch,const PumpHeatingLimits& limits);
bool readback_matches(const Reading& before,const Reading& after,const Patch& patch);
PumpRoomMode heating_mode(const Reading& reading);
}
