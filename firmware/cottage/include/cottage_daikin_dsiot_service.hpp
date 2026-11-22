#pragma once
#include "pump/pump_adapter.hpp"
class WebServer;
bool cottage_daikin_dsiot_selected();
bool cottage_daikin_dsiot_needs_worker();
void cottage_daikin_dsiot_register_routes(WebServer& web);
void cottage_daikin_dsiot_tick();
uint32_t cottage_daikin_dsiot_configuration_generation();
uint32_t cottage_daikin_dsiot_calibration_identity();
enevina::PumpRoomState cottage_daikin_dsiot_room_state();
