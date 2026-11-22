#pragma once
#include "cottage/mitsubishi_control_policy.hpp"

class WebServer;
bool cottage_daikin_selected();
bool cottage_daikin_needs_worker();
void cottage_daikin_register_routes(WebServer& server);
void cottage_daikin_tick();
uint32_t cottage_daikin_configuration_generation();
enevina::PumpRoomState cottage_daikin_room_state();

bool cottage_daikin_automatic_ready();
bool cottage_daikin_automatic_enabled();
void cottage_daikin_automatic_cancel();
uint32_t cottage_daikin_calibration_identity();
bool cottage_daikin_automatic_write_current(uint32_t authorization,uint32_t config,
    uint32_t settings,const enevina::cottage::BootstrapDecision& proposal);
enevina::cottage::ControlBlock cottage_daikin_submit_automatic(const enevina::PumpRoomState&,
    enevina::cottage::ControlSnapshot,uint32_t authorization,uint32_t config,uint32_t settings);
