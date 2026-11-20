#pragma once
// Midea V3 (8370) LAN framing for an ESP32-S3 client. No Python/cloud runtime.
// Wire format independently implemented using msmart-ng's documented protocol.
// No device secrets are embedded in source or exposed through this interface.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace enevina::midea {
using Bytes=std::vector<uint8_t>;
struct State {
    bool power=false;
    uint8_t mode=0xff;    // Midea mode value (HEAT=4)
    double setpoint_c=0;
    double indoor_c=0;
    double outdoor_c=0;
    bool has_indoor=false,has_outdoor=false;
    uint8_t fan=0,swing=0;
    bool turbo=false,eco=false,sleep=false,fahrenheit=false;
    bool follow_me=false,purifier=false,aux_heat=false,freeze_protection=false;
    bool freeze_protection_known=false; // absent on short C0, never assume off
    bool independent_aux_heat=false; // C0 byte 8 bit 0x40
    uint8_t humidity=40;
};
// The AC 0x40 command sets the full unit state. Only explicitly named
// fields change; other known values originate from the last verified C0.
enum ControlField : uint8_t {
    ControlPower=1u, ControlMode=2u, ControlTemperature=4u,
    ControlFan=8u, ControlSwing=16u
};
struct ControlPatch {
    uint8_t changed=0;
    bool power=false;
    uint8_t mode=0xff;
    double setpoint_c=0.0;
    uint8_t fan=102;
    uint8_t swing=0;
};
bool valid_control_patch(const ControlPatch& patch);
bool set_state_command(const State& observed,const ControlPatch& patch,
                       uint8_t message_id,Bytes& frame);
bool control_patch_observed(const State& actual,const ControlPatch& patch);
// Separate readback must also show that unmodified and safety-critical
// state survived a whole-state 0x40 command.
bool preserved_state_observed(const State& before,const State& after,
                              const ControlPatch& patch);

struct Credential {
    uint64_t device_id=0;
    std::array<uint8_t,64> token{};
    std::array<uint8_t,32> key{};
};
// Accepted only when all lengths/hex characters are valid. Do not log input.
bool decode_credentials(const char* id,const char* token_hex,const char* key_hex,Credential& out);
// V2 device ID is not a pairing secret; V2 has no V3 token/key exchange.
bool decode_device_id(const char* id,uint64_t& out);
// V2 transport packet length, little endian, strict bounds (40 + encrypted + 16).
bool v2_packet_size(const uint8_t* header,size_t n,size_t& total);
// Validate V2 outer packet's device ID before decoding its signed payload.
// This is an integrity check, NOT authentication (V2 uses a shared fixed key).
bool v2_packet_matches_device(const uint8_t* packet,size_t n,uint64_t device_id);
Bytes handshake_request(const Credential&,uint16_t sequence=0);
// 8370 response MUST pass SHA-256 check before deriving AES256 local key.
bool handshake_response(const uint8_t* packet,size_t n,const Credential&,
                        std::array<uint8_t,32>& session_key);
// Binary AC get-state query, including 0xAA frame checksum and payload CRC.
Bytes state_query(uint8_t message_id);
// Signed and encrypted 5A5A LAN inner packet; UTC timestamp supplied by caller.
bool inner_encode(uint64_t device_id,const uint8_t* command,size_t command_size,
                  const uint8_t utc_timestamp[8],Bytes& out);
bool inner_decode(const uint8_t* packet,size_t n,Bytes& frame);
// Authenticated AES256 8370 transport envelope. Caller supplies random padding.
bool encrypted_request(const uint8_t* inner,size_t n,
                       const std::array<uint8_t,32>& session_key,
                       uint16_t sequence,const uint8_t* pad,size_t pad_capacity,Bytes& out);
bool encrypted_response(const uint8_t* packet,size_t n,
                        const std::array<uint8_t,32>& session_key,Bytes& inner);
// Strict checksumming and supported AC state field decoder, not a packet receipt timestamp.
bool parse_state(const uint8_t* frame,size_t n,State& out);
// Nonsecret validation reason for troubleshooting device-specific read frames.
// 0 = valid, 1=length, 2=frame magic/device, 3=declared length,
// 4=not a C0 status, 5=frame checksum, 6=payload CRC, 7=state range.
uint8_t parse_state_failure_code(const uint8_t* frame,size_t n);

bool valid_state(const State& s);
// Midea's expected V3 8370 network packet length from the six-byte header.
bool packet_size(const uint8_t* header,size_t n,size_t& total);
// Convert a byte array to uppercase/lowercase-free hex without local/device identifiers.
bool decode_hex(const char* hex,uint8_t* dst,size_t dst_size);
} // namespace enevina::midea