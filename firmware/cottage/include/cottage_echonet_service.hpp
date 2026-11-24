#pragma once
#include "cottage/mitsubishi_control_policy.hpp"
class WebServer;
bool cottage_echonet_selected();
bool cottage_echonet_needs_worker();
void cottage_echonet_register_routes(WebServer&);
void cottage_echonet_tick();
uint32_t cottage_echonet_configuration_generation();
uint32_t cottage_echonet_calibration_identity();
enevina::PumpRoomState cottage_echonet_room_state();
bool cottage_echonet_automatic_ready();
bool cottage_echonet_automatic_enabled();
void cottage_echonet_automatic_cancel();
bool cottage_echonet_automatic_write_current(uint32_t authorization,uint32_t config,
    uint32_t settings,const enevina::cottage::BootstrapDecision&);
enevina::cottage::ControlBlock cottage_echonet_submit_automatic(const enevina::PumpRoomState&,
    enevina::cottage::ControlSnapshot,uint32_t authorization,uint32_t config,uint32_t settings);
