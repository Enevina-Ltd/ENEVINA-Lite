#pragma once
// Phase-1 Wi-Fi commissioning for the headless ESP32-S3 (see
// cottage/wifi_provisioning.hpp for the host-tested state machine).
class WebServer;

// setup(): load the stored network (NVS, else compile-time default), start
// STA and, when needed, the per-device WPA2 setup AP; register /setup and
// /api/wifi/* routes. Call before web.begin().
void cottage_wifi_begin(WebServer& web);
// loop(): captive DNS, state machine tick, AP open/close, NVS commit, mDNS.
void cottage_wifi_loop();
// True when the current HTTP request arrived through the setup AP.
bool cottage_wifi_request_via_ap(WebServer& web);
// Minimal phase-1 page (Wi-Fi only).
extern const char cottage_wifi_setup_html[];
