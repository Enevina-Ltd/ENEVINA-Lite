#pragma once
#include "pump/pump_adapter.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace enevina::daikin {
constexpr size_t max_body=2048;
constexpr std::string_view identity_path="/common/basic_info";
constexpr std::string_view state_path="/aircon/get_control_info";
constexpr std::string_view sensor_path="/aircon/get_sensor_info";
struct Device {
    std::string mac;
    std::string firmware;
    std::string model;
};
struct Reading {
    bool power=false;
    unsigned mode=0;
    std::optional<double> target_c;
    std::string target,humidity,fan,swing;
    std::string heat_humidity,heat_fan,heat_swing;
};
struct Temperatures {
    std::optional<double> indoor_c,outdoor_c;
    std::optional<double> compressor_hz;
};
struct Patch {
    std::optional<bool> power;
    std::optional<double> heat_target_c;
    bool heat_mode=false;
};

std::optional<Device> decode_identity(std::string_view reply);
std::optional<Reading> decode_state(std::string_view reply);
std::optional<Temperatures> decode_sensors(std::string_view reply);
std::optional<std::string> set_request(const Reading& current,const Patch& patch,
                                       const PumpHeatingLimits& limits);
bool accepted(std::string_view reply);
bool matches(const Reading& state,const Patch& patch);
PumpRoomMode room_mode(const Reading& state);
}
