#pragma once
#include "pump/pump_adapter.hpp"
class WebServer;
bool cottage_wf_rac_selected();
bool cottage_wf_rac_needs_worker();
void cottage_wf_rac_register_routes(WebServer& web);
void cottage_wf_rac_tick();
enevina::PumpRoomState cottage_wf_rac_room_state();
uint32_t cottage_wf_rac_configuration_generation();
uint32_t cottage_wf_rac_calibration_identity();
