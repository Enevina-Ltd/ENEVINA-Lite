#pragma once
#include "pump/pump_adapter.hpp"

class WebServer;
bool cottage_daikin_selected();
bool cottage_daikin_needs_worker();
void cottage_daikin_register_routes(WebServer& server);
void cottage_daikin_tick();
uint32_t cottage_daikin_configuration_generation();
enevina::PumpRoomState cottage_daikin_room_state();
