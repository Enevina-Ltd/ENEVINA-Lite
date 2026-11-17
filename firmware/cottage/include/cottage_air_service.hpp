#pragma once
#include <cstdint>
#include "cottage/telemetry.hpp"
#include "cJSON.h"
class WebServer;
namespace enevina::cottage {
void c23_initialize();
void c23_ble_worker(void*);
void c23_tick();
TemperatureSample c23_temperature_sample();
void c23_append_status(cJSON* root);
void c23_history_http(WebServer& server);
void c23_register_routes(WebServer& server);
} // namespace enevina::cottage
