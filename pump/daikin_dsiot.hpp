#pragma once
#include "pump/pump_adapter.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace enevina::dsiot {
constexpr size_t max_document=16*1024;
constexpr std::string_view endpoint="/dsiot/multireq";
constexpr std::string_view indoor_path="/dsiot/edge/adr_0100.dgc_status";
constexpr std::string_view outdoor_path="/dsiot/edge/adr_0200.dgc_status";
constexpr std::string_view identity_path="/dsiot/edge.adp_i";
enum class Mode { Fan, Heat, Cooling, Auto, Dry, Unknown };
struct Device {
    std::string mac,firmware,api_version,model;
};
struct Reading {
    Device device{};
    Mode mode=Mode::Unknown;
    bool power=false;
    std::optional<double> target_c,indoor_c,outdoor_c;
};
struct Patch {
    std::optional<bool> power;
    std::optional<double> target_c;
    bool heat=false;
};

std::string status_request();
std::optional<Reading> decode_status(std::string_view body);
std::optional<std::string> write_request(const Reading& before,const Patch&,
                                         const PumpHeatingLimits& limits);
bool write_accepted(std::string_view body);
bool readback_matches(const Reading& before,const Reading& after,const Patch& patch);
PumpRoomMode heating_mode(const Reading& reading);
}
