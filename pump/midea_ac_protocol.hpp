#pragma once
#include "pump/midea_v3_protocol.hpp"

namespace enevina::midea {
enum class ResponseKind : uint8_t { Invalid, BasicState, Auxiliary, Capabilities, Subtype };
constexpr uint32_t AcGeneralFields=ControlPower|ControlMode|ControlTemperature|
    ControlFan|ControlSwing|ControlEco|ControlTurbo|ControlSleep|
    ControlFreezeProtection|ControlAnion;
constexpr uint32_t AcPropertyFields=ControlDisplay|ControlVerticalAngle|
    ControlHorizontalAngle|ControlRateSelect|ControlBreezeless|
    ControlIndirectWind|ControlSelfClean|ControlSound;

Bytes subtype_query(uint8_t protocol_version=0);
bool parse_subtype(const uint8_t* frame,size_t n,AcProfile& profile);
Bytes ac_state_query(const AcProfile& profile,uint8_t message_id);
// Group 0: BB indoor (10), 1: BB outdoor (30), 2: supported B1 properties,
// 3: alternate A0 state query, 4: independent self-clean property query,
// 5: independent rate-select property query for an explicit control request.
// An empty result means no applicable query.
Bytes ac_auxiliary_query(const AcProfile& profile,uint8_t message_id,uint8_t group);
// Parsing is atomic. BasicState replaces live fields (including missing-field
// markers) while retaining profile metadata; Auxiliary never proves a new
// basic state. Invalid frames leave state completely unchanged.
ResponseKind ingest_ac_response(const uint8_t* frame,size_t n,State& state);
// Rejects mixed general/property commands and multiple enabled presets. B0
// display requires a current 0x0017 observation, not generic B5 display support.
bool valid_ac_control_patch(const State& state,const ControlPatch& patch);
// One enabled preset clears the other native presets. Leaving Dry selects
// Auto fan unless the patch explicitly supplies a supported fan. Classic full
// writes require fresh preservation evidence; selective B0 writes do not.
bool ac_set_state_command(const State& observed,const ControlPatch& patch,
                          uint8_t message_id,Bytes& frame);
// Confirms the expected transition and all serialized preserved fields from
// an independent current readback, including unchanged target humidity.
bool ac_control_confirmed(const State& before,const State& after,
                          const ControlPatch& patch);
} // namespace enevina::midea
