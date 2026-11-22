#pragma once
#include "cottage/mitsubishi_control_policy.hpp"
class WebServer;
bool cottage_panasonic_selected();
bool cottage_panasonic_needs_worker();
void cottage_panasonic_register_routes(WebServer&);
void cottage_panasonic_tick();
uint32_t cottage_panasonic_configuration_generation();
uint32_t cottage_panasonic_calibration_identity();
enevina::PumpRoomState cottage_panasonic_room_state();

uint32_t cottage_panasonic_bus_generation();
bool cottage_panasonic_automatic_ready();
bool cottage_panasonic_automatic_enabled();
void cottage_panasonic_automatic_cancel();
bool cottage_panasonic_automatic_write_current(uint32_t authorization,uint32_t config,
    uint32_t settings,const enevina::cottage::BootstrapDecision& proposal);
enevina::cottage::ControlBlock cottage_panasonic_submit_automatic(const enevina::PumpRoomState&,
    enevina::cottage::ControlSnapshot,uint32_t authorization,uint32_t config,uint32_t settings);
