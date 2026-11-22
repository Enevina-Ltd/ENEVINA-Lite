#pragma once

#include "pump/pump_adapter.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace enevina::panasonic {
enum class Family { Cnt, Wlan };
enum class Mode : uint8_t { Unknown=0, Auto=1, Cool=2, Dry=3, Heat=4, Fan=5 };
struct Packet {
    std::array<uint8_t,160> bytes{};
    size_t size=0;
    uint8_t sequence() const {return bytes[1];}
    bool is_wlan(uint8_t group,uint8_t code) const;
};
class Reader {
public:
    std::optional<Packet> append(uint8_t byte);
private:
    Packet pending_{};
    size_t expected_=0;
};
struct Climate {
    Family family=Family::Cnt;
    Mode mode=Mode::Unknown;
    bool power=false;
    std::optional<bool> defrost;
    std::optional<double> target_c,indoor_c,outdoor_c;
    std::array<uint8_t,10> cnt_control{};
};
struct Patch {
    std::optional<bool> power,heat;
    std::optional<double> target_c;
};

uint8_t next_sequence(uint8_t sequence);
Packet cnt_poll();
Packet wlan_poll(uint8_t sequence);
Packet wlan_reply(const Packet& request,const std::array<uint8_t,6>& module_mac={});
struct HandshakeStep {
    Packet packet;
    uint8_t reply_group=0,reply_code=0;
};
// Steps 1 through 13 transmit numbered requests. The final 0x01/0x80
// reply follows remote initiation, the first full query and a settling period.
HandshakeStep wlan_handshake(unsigned step,uint8_t sequence);
std::optional<Climate> climate(const Packet& reply,Family family,uint8_t sequence=0);
std::optional<bool> defrost_status(const Packet& packet);
bool same_settings(const Climate& a,const Climate& b);
std::optional<Packet> command(const Climate& before,const Patch& patch,
                              const PumpHeatingLimits& limits,uint8_t sequence=0);
bool readback_matches(const Climate& before,const Climate& after,const Patch& patch);
} // namespace enevina::panasonic
