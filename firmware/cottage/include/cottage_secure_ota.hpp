#pragma once
#include <cstdint>
// User approval is staging a publisher-signed release via authenticated
// filesystem/SSH on the read-only HTTPS server. The ESP32 has no OTA POST route.
bool cottage_ota_configured();
void cottage_ota_initialize();
void cottage_ota_worker(void*);
// Ask the existing signed-HTTPS OTA worker to check/apply the staged release
// immediately (same manifest signature, TLS, SHA256, A/B and rollback gates).
// Returns false if the signed channel or worker is not available.
bool cottage_ota_request_immediate();
bool cottage_ota_dashboard_request_pending();
uint32_t cottage_ota_last_check_utc();
bool cottage_ota_confirmation_pending();
// True only after the persisted OTA transaction has been successfully checked.
// False while site/air early boot recovery is running, even on a VALID image.
bool cottage_ota_runtime_ready();
// True only after durable good-version and pending-transaction cleanup.
bool cottage_ota_confirm_boot();
uint32_t cottage_ota_good_version();
uint32_t cottage_ota_last_candidate();
int cottage_ota_status();
// Temporary read-only worker pause during eligible signed-image download.
// Never persists user settings and never changes an automation authorization.
bool cottage_ota_network_maintenance();
uint32_t cottage_ota_downloaded_bytes();
uint32_t cottage_ota_chunk_count();
uint32_t cottage_ota_idle_yields();