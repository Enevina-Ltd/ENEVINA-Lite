#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace enevina::gree {
constexpr uint16_t port=7000;
constexpr size_t max_packet=1400;
constexpr std::string_view scan_packet=R"({"t":"scan"})";
enum class Cipher : uint8_t { Ecb=1, Gcm=2 };
enum class Mode : uint8_t { Auto=0, Cool=1, Dry=2, Fan=3, Heat=4 };
using Key=std::array<uint8_t,16>;

struct Discovery {
    Cipher cipher;
    std::string id;
    std::string wire_id;
};
struct Reading {
    bool power=false;
    Mode mode=Mode::Auto;
    std::optional<int> setpoint_c;
    std::optional<int> indoor_c;
    std::optional<bool> frost;
};
struct Patch {
    std::optional<bool> power;
    std::optional<Mode> mode;
    std::optional<int> setpoint_c;
};

std::optional<std::string> device_id(std::string_view value);
const Key& binding_key(Cipher cipher);
std::string pack(Cipher cipher,const Key& key,bool enrollment,
                 std::string_view from,std::string_view to,std::string_view payload);
std::optional<std::string> unpack(Cipher cipher,const Key& key,bool enrollment,
                 std::string_view from,std::string_view to,std::string_view datagram);
std::optional<Discovery> discovery(std::string_view datagram);
std::optional<Key> bound_key(Cipher cipher,std::string_view device,std::string_view datagram);
std::string bind_request(Cipher cipher,std::string_view device);
std::string status_request(Cipher cipher,const Key& key,std::string_view device);
std::optional<Reading> parse_status(Cipher cipher,const Key& key,std::string_view device,
                                    std::string_view datagram);
std::string command_request(Cipher cipher,const Key& key,std::string_view device,const Patch& patch);
bool confirmed(const Patch& patch,const Reading& reading);
}
