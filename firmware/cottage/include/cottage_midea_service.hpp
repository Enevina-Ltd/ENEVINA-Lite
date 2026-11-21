#pragma once
#include <cstdint>
// Standalone local Midea V2/V3 service with explicit manual control.
// The selected Midea never enters the Mitsubishi CN105 writer,
// and unverified Midea control is not triggered by automatic optimization.
#ifndef COTTAGE_BENCH_MODE
class WebServer;
void cottage_midea_service_initialize();
void cottage_midea_service_register_routes(WebServer& web);
void cottage_midea_service_worker(void*);
// True only for a selected Midea adapter with committed LAN credentials.
// Config and status routes continue to work even before the worker starts.
bool cottage_midea_service_needs_worker();
// Nonblocking identity guards for the transport's cached negotiation and final
// command gate. A transition in progress is never a current configuration.
uint32_t cottage_midea_configuration_generation();
bool cottage_midea_configuration_current(uint32_t expected_generation);
#endif
// Wi-Fi event callback: invalidate queued physical commands immediately.
void cottage_midea_link_disconnected();
