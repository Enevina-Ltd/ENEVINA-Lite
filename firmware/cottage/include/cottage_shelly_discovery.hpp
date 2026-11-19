#pragma once
// Asynchronous Shelly discovery on the home LAN (mDNS + two small GETs).
// One worker task does the blocking work; web handlers only start and poll it.
// Read-only: no relay command is ever sent.
#include "cottage/shelly_discovery.hpp"
#include <cstddef>
#include <cstdint>

namespace enevina_firmware {
enum class ShellyScanState : uint8_t { Idle=0, Pending=1, Done=2, Failed=3 };
constexpr size_t kShellyMaxFound=6;
struct ShellyFound {
    char host[48]{};
    char url[24]{};                       // http://a.b.c.d (private IPv4 only)
    enevina::cottage::ShellyInfo info{};
    bool has_power=false;                 // live apower read succeeded
    double watts=0;
};
struct ShellyScanSnapshot {
    ShellyScanState state=ShellyScanState::Idle;
    size_t count=0;
    ShellyFound items[kShellyMaxFound]{};
    uint32_t age_ms=0;                    // since the scan finished
    bool busy_cooldown=false;             // a new scan was refused (too soon)
};
// Starts a scan when none runs and the last one is older than 8 s, otherwise
// returns the current/last snapshot. Never blocks for the network.
ShellyScanState shelly_scan(ShellyScanSnapshot& out);
}
