#pragma once
class WebServer;
#include "pump/pump_adapter.hpp"

bool cottage_gree_selected();
bool cottage_gree_needs_worker();
void cottage_gree_register_routes(WebServer& server);
void cottage_gree_worker(void*);
enevina::PumpRoomState cottage_gree_room_state();
