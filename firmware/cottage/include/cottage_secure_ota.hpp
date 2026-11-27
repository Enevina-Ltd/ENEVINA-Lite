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
// Last upgrade proven by signed-OTA confirmation, durable NVS marker when
// available. Zero on factory first boot, unconfirmed image or rollback.
uint32_t cottage_ota_recorded_upgrade_version();
// Called after the history events file has been durably flushed to LittleFS.
bool cottage_ota_acknowledge_history_upgrade(uint32_t version,uint32_t event_utc);
// Durable last update (signed version + trusted time) independent of event ring.
bool cottage_ota_last_successful_update(uint32_t& utc,uint32_t& version);
uint32_t cottage_ota_last_candidate();
int cottage_ota_status();
// Read-only diagnostics for last signed-OTA attempt. No tokens, URLs or keys.
const char* cottage_ota_problem_stage();
const char* cottage_ota_problem_reason();
uint32_t cottage_ota_problem_free_internal();
uint32_t cottage_ota_problem_largest_internal();
uint32_t cottage_ota_image_lease_attempts();
uint32_t cottage_ota_inflight_memory_waits();
// Temporary read-only worker pause during final verified-image installation.
// Never persists user settings and never changes an automation authorization.
bool cottage_ota_network_maintenance();
// Owner-task verified persistence of all learned state before activation.
bool cottage_ota_checkpoint_learning();
uint32_t cottage_ota_downloaded_bytes();
uint32_t cottage_ota_expected_bytes();
uint32_t cottage_ota_resume_count();
bool cottage_ota_reconnecting();
uint32_t cottage_ota_programmed_bytes();
uint32_t cottage_ota_chunk_count();
uint32_t cottage_ota_idle_yields();