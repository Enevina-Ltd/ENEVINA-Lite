#pragma once
#include <cstdint>
// User approval is staging a publisher-signed release via authenticated
// filesystem/SSH on the read-only HTTPS server. The ESP32 has no OTA POST route.
bool cottage_ota_configured();
void cottage_ota_initialize();
void cottage_ota_worker(void*);
bool cottage_ota_confirmation_pending();
// True only after durable good-version and pending-transaction cleanup.
bool cottage_ota_confirm_boot();
uint32_t cottage_ota_good_version();
uint32_t cottage_ota_last_candidate();
int cottage_ota_status();
