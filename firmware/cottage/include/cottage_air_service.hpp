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
CottageModeConfig c23_mode_config();
CottageModePlan c23_current_mode_plan();
uint32_t c23_config_generation();
bool c23_authorize_local_setting(const char* submitted_token);
void c23_append_status(cJSON* root);
void c23_history_http(WebServer& server);
void c23_register_routes(WebServer& server);
} // namespace enevina::cottage
