#pragma once
#include "cottage/mitsubishi_control_policy.hpp"
class WebServer;
bool cottage_cn105_direct_selected();
bool cottage_cn105_direct_needs_worker();
void cottage_cn105_direct_register_routes(WebServer&);
void cottage_cn105_direct_tick();
uint32_t cottage_cn105_direct_configuration_generation();
uint32_t cottage_cn105_direct_calibration_identity();
enevina::PumpRoomState cottage_cn105_direct_room_state();

uint32_t cottage_cn105_direct_bus_generation();
bool cottage_cn105_direct_automatic_ready();
bool cottage_cn105_direct_automatic_enabled();
void cottage_cn105_direct_automatic_cancel();
bool cottage_cn105_direct_automatic_write_current(uint32_t authorization,uint32_t config,
    uint32_t settings,const enevina::cottage::BootstrapDecision& proposal);
enevina::cottage::ControlBlock cottage_cn105_direct_submit_automatic(const enevina::PumpRoomState&,
    enevina::cottage::ControlSnapshot,uint32_t authorization,uint32_t config,uint32_t settings);
