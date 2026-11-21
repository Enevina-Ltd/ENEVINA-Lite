#pragma once
#include "pump/midea_discovery.hpp"

namespace enevina::midea {
struct DiscoveryInfo {
    DiscoveredDevice device{};
    uint8_t device_type=0;
    uint16_t port=6444,subtype=0;
    bool subtype_known=false;
    std::array<char,9> model{};
};
// Integrity-checked metadata is a discovery hint, never a write authorization.
// It intentionally excludes serial number, MAC and network SSID from output.
bool parse_discovery_info(const uint8_t* packet,size_t length,DiscoveryInfo& out);
}
