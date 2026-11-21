#pragma once
#include <cstdint>
// Local Midea V2/V3 service owning one shared manual/automatic write queue.
#ifndef COTTAGE_BENCH_MODE
#include "cottage/mitsubishi_control_policy.hpp"
class WebServer;
bool cottage_midea_selected();
bool cottage_midea_automatic_ready();
bool cottage_midea_automatic_enabled();
void cottage_midea_automatic_cancel();
bool cottage_midea_automatic_write_current(uint32_t authorization_generation,
    uint32_t control_config_generation,uint32_t site_generation);
bool cottage_midea_automatic_proposal_current(enevina::cottage::BootstrapAction,double);
enevina::PumpRoomState cottage_midea_room_state();
enevina::cottage::ControlBlock cottage_midea_submit_automatic(
    const enevina::PumpRoomState& state,enevina::cottage::ControlSnapshot decision,
    uint32_t authorization_generation,uint32_t control_config_generation,uint32_t site_generation);
void cottage_midea_service_initialize();
void cottage_midea_service_register_routes(WebServer& web);
void cottage_midea_service_worker(void*);
// True only for a selected Midea adapter with committed LAN credentials.
// Config and status routes continue to work even before the worker starts.
bool cottage_midea_service_needs_worker();
// Nonblocking identity guards for the transport's cached negotiation and final
// command gate. A transition in progress is never a current configuration.
uint32_t cottage_midea_configuration_generation();
uint32_t cottage_midea_calibration_identity();
bool cottage_midea_configuration_current(uint32_t expected_generation);
#endif
// Wi-Fi event callback: invalidate queued physical commands immediately.
void cottage_midea_link_disconnected();
