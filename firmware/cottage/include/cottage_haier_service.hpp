#pragma once
#include "cottage/mitsubishi_control_policy.hpp"
class WebServer;
bool cottage_haier_selected();
bool cottage_haier_needs_worker();
void cottage_haier_register_routes(WebServer&);
void cottage_haier_tick();
uint32_t cottage_haier_configuration_generation();
uint32_t cottage_haier_calibration_identity();
enevina::PumpRoomState cottage_haier_room_state();

uint32_t cottage_haier_bus_generation();
bool cottage_haier_automatic_ready();
bool cottage_haier_automatic_enabled();
void cottage_haier_automatic_cancel();
bool cottage_haier_automatic_write_current(uint32_t authorization,uint32_t config,
    uint32_t settings,const enevina::cottage::BootstrapDecision& proposal);
enevina::cottage::ControlBlock cottage_haier_submit_automatic(const enevina::PumpRoomState&,
    enevina::cottage::ControlSnapshot,uint32_t authorization,uint32_t config,uint32_t settings);
