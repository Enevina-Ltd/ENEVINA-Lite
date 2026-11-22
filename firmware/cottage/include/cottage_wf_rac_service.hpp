#pragma once
#include "cottage/mitsubishi_control_policy.hpp"
class WebServer;
bool cottage_wf_rac_selected();
bool cottage_wf_rac_needs_worker();
void cottage_wf_rac_register_routes(WebServer& web);
void cottage_wf_rac_tick();
enevina::PumpRoomState cottage_wf_rac_room_state();
uint32_t cottage_wf_rac_configuration_generation();
uint32_t cottage_wf_rac_calibration_identity();

bool cottage_wf_rac_automatic_ready();
bool cottage_wf_rac_automatic_enabled();
void cottage_wf_rac_automatic_cancel();
bool cottage_wf_rac_automatic_write_current(uint32_t authorization,uint32_t config,
    uint32_t settings,const enevina::cottage::BootstrapDecision& proposal);
enevina::cottage::ControlBlock cottage_wf_rac_submit_automatic(const enevina::PumpRoomState&,
    enevina::cottage::ControlSnapshot,uint32_t authorization,uint32_t config,uint32_t settings);
