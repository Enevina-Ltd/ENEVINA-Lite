#pragma once
#include <cstdint>
#include "cottage/telemetry.hpp"
#include "cottage/air_humidity.hpp"
#include "cJSON.h"
class WebServer;
namespace enevina::cottage {
void c23_initialize();
void c23_ble_worker(void*);
bool c23_ble_initialized();
void c23_tick();
TemperatureSample c23_temperature_sample();
// Fresh indoor humidity and a FRESH measured outdoor temperature (outdoor
// RuuviTag), each with its own validity flag. Never returns a stale value.
void c23_history_air(double& rh_pct,bool& rh_ok,double& outdoor_c,bool& outdoor_ok);
// True when an indoor RuuviTag is bound (fresh or not).
bool c23_indoor_bound();
CottageModeConfig c23_mode_config();
CottageModePlan c23_current_mode_plan();
uint32_t c23_config_generation();
bool c23_config_storage_verified();
// Separate from the pending journal gate: verify the four real mode NVS keys
// when aborting an interrupted cross-namespace Comfort transaction.
bool c23_mode_config_is_durable(const CottageModeConfig& cfg);
bool c23_prepare_comfort_base(const CottageModeConfig& cfg);
uint32_t c23_indoor_binding_id();
bool c23_authorize_local_setting(const char* submitted_token);
// Existing per-boot local CSRF token (also published in /api/status).
const char* c23_config_token();
void c23_append_status(cJSON* root);
void c23_history_http(WebServer& server);
void c23_register_routes(WebServer& server);
} // namespace enevina::cottage
