#pragma once
// Direct local Midea V3 TCP client on ESP32-S3; no Python, HA or cloud runtime.
// Not enabled as a physical Lite control path until separate model review.
#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <IPAddress.h>
#include "pump/midea_v3_protocol.hpp"

namespace enevina::midea {
enum class ReadStatus : uint8_t {
    Ok, InvalidAddress, InvalidCredentials, ConnectionFailed,
    AuthenticationFailed, ProtocolFailed, UnsupportedState, TimeNotReady
};
ReadStatus read_device(const char* private_ipv4,const Credential& credentials,
                       State& reading);
// Writes deliberately NOT exposed until full state preservation, readback,
// model-specific constraints and the existing Lite Supervisor are integrated.
} // namespace enevina::midea
#endif