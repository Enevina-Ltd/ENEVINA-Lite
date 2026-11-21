#pragma once
// Direct local Midea V2/V3 TCP client on ESP32-S3; no Python, HA or cloud runtime.
// Manual and automatic writes share the same serialized service.
#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <IPAddress.h>
#include "pump/midea_v3_protocol.hpp"
#include "cottage/price_bootstrap.hpp"

namespace enevina::midea {
enum class ReadStatus : uint8_t {
    Ok, InvalidAddress, InvalidCredentials, ConnectionFailed,
    AuthenticationFailed, ProtocolFailed, UnsupportedState, TimeNotReady, ControlNotConfirmed, ResourceBusy
};
struct AutomaticWriteContext {
    uint32_t authorization_generation=0,control_config_generation=0,site_generation=0;
    bool power=false;
    uint8_t mode=0xff;
    double target_c=0;
    cottage::BootstrapAction proposal_action=cottage::BootstrapAction::Unavailable;
    double proposal_target_c=0;
};
struct WriteReceipt {
    bool attempted=false;
    bool sent=false;
};
uint8_t last_v3_read_detail(); // Nonsecret live-read failure classification.
uint8_t last_v3_reply_opcode(); // Nonsecret Midea AC message type.
ReadStatus read_device(const char* private_ipv4,const Credential& credentials,
                       State& reading,uint32_t expected_configuration_generation);
// Legacy V2 uses direct 5A5A AES-128-ECB + MD5 packets with NO V3 handshake
// and NO token/key. Keep explicit protocol
// selection: never silently downgrade failed V3 authentication to V2.
ReadStatus read_device_v2(const char* private_ipv4,uint64_t device_id,
                          State& reading,uint32_t expected_configuration_generation);
// Explicit Midea LAN controls. These are never called by polling and
// never retry a physical SET after an uncertain write. Each call reads current
// state first and requires fresh, family-matched independent readback.
// Call from a dedicated worker, never the HTTP request handler.
// The caller is responsible for selected-device and user-authorization gates.
ReadStatus write_device_v2(const char* private_ipv4,uint64_t device_id,
                           const ControlPatch& patch,State& confirmed,
                           uint32_t expected_wifi_generation,
                           uint32_t expected_endpoint_generation,
                           uint32_t expected_configuration_generation,
                           int64_t command_deadline_utc,
                           double approved_min_c,double approved_max_c,
                           const AutomaticWriteContext* automatic=nullptr,
                           WriteReceipt* receipt=nullptr);
ReadStatus write_device(const char* private_ipv4,const Credential& credentials,
                        const ControlPatch& patch,State& confirmed,
                           uint32_t expected_wifi_generation,
                           uint32_t expected_endpoint_generation,
                           uint32_t expected_configuration_generation,
                           int64_t command_deadline_utc,
                           double approved_min_c,double approved_max_c,
                           const AutomaticWriteContext* automatic=nullptr,
                           WriteReceipt* receipt=nullptr);
} // namespace enevina::midea
#endif
