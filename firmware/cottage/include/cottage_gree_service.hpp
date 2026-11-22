#pragma once
#include "cottage/mitsubishi_control_policy.hpp"

class WebServer;
bool cottage_gree_selected();
bool cottage_gree_needs_worker();
void cottage_gree_register_routes(WebServer& server);
void cottage_gree_tick();
enevina::PumpRoomState cottage_gree_room_state();
uint32_t cottage_gree_calibration_identity();
uint32_t cottage_gree_configuration_generation();
bool cottage_gree_automatic_ready();
bool cottage_gree_automatic_enabled();
void cottage_gree_automatic_cancel();
bool cottage_gree_automatic_write_current(uint32_t authorization,uint32_t config,
    uint32_t settings,const enevina::cottage::BootstrapDecision&);
enevina::cottage::ControlBlock cottage_gree_submit_automatic(const enevina::PumpRoomState&,
    enevina::cottage::ControlSnapshot,uint32_t authorization,uint32_t config,uint32_t settings);
