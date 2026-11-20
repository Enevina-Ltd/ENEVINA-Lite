#pragma once
// Direct local Midea V2/V3 TCP client on ESP32-S3; no Python, HA or cloud runtime.
// Not enabled as a physical Lite control path until separate model review.
#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <IPAddress.h>
#include "pump/midea_v3_protocol.hpp"

namespace enevina::midea {
enum class ReadStatus : uint8_t {
    Ok, InvalidAddress, InvalidCredentials, ConnectionFailed,
    AuthenticationFailed, ProtocolFailed, UnsupportedState, TimeNotReady, ControlNotConfirmed, ResourceBusy
};
uint8_t last_v3_read_detail(); // Nonsecret live-read failure classification.
uint8_t last_v3_reply_opcode(); // Nonsecret Midea AC message type.
ReadStatus read_device(const char* private_ipv4,const Credential& credentials,
                       State& reading);
// Legacy V2 uses direct 5A5A AES-128-ECB + MD5 packets with NO V3 handshake
// and NO token/key. Both transports are read-only. Keep explicit protocol
// selection: never silently downgrade failed V3 authentication to V2.
ReadStatus read_device_v2(const char* private_ipv4,uint64_t device_id,
                          State& reading);
// Explicit Midea LAN controls. These are never called by polling and
// never retry a physical 0x40 command after an uncertain write. Each call
// reads current state first and requires fresh post-write 0x41 readback.
// Call from a dedicated worker, never the HTTP request handler.
// The caller is responsible for selected-device and user-authorization gates.
ReadStatus write_device_v2(const char* private_ipv4,uint64_t device_id,
                           const ControlPatch& patch,State& confirmed,
                           uint32_t expected_wifi_generation,
                           int64_t command_deadline_utc,
                           double approved_min_c,double approved_max_c);
ReadStatus write_device(const char* private_ipv4,const Credential& credentials,
                        const ControlPatch& patch,State& confirmed,
                           uint32_t expected_wifi_generation,
                           int64_t command_deadline_utc,
                           double approved_min_c,double approved_max_c);
} // namespace enevina::midea
#endif