#pragma once
// Standalone read-only Midea LAN service. The selected Midea never enters
// the Mitsubishi CN105 physical write path or masquerades as CN105 telemetry.
#ifndef COTTAGE_BENCH_MODE
class WebServer;
void cottage_midea_service_initialize();
void cottage_midea_service_register_routes(WebServer& web);
void cottage_midea_service_worker(void*);
#endif