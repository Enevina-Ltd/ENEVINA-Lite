#pragma once
// Phase-2 browser configuration persisted in NVS ("site" namespace).
// Compile-time COTTAGE_* values remain the fallback for an unconfigured
// device, so the existing pilot build keeps working unchanged.
#include "cottage/site_settings.hpp"
#include "cottage/comfort_transaction.hpp"
#include "cottage/telemetry.hpp"
class WebServer;

void site_settings_initialize();             // once in setup(), before workers
enevina::cottage::SiteSettings site_settings_snapshot(); // thread-safe copy
uint32_t site_settings_generation();         // increments on every commit
enevina::cottage::RoomTemperatureConfig site_room_temperature_config();
// Increments only when the pump identity changes (CN105 URL or adapter).
// Armed CN105 control is bound to the identity it was enabled for.
uint32_t site_settings_endpoint_generation();
// Revoke the previous pump's durable write permission BEFORE saving a new
// CN105 URL or adapter. Returns false if NVS revocation failed.
bool cottage_control_revoke_saved_intent();
void site_settings_register_routes(WebServer& web);
// Crash-recovery journal for wizard Comfort: written BEFORE site limits,
// completed by durable air save; a pending journal blocks automatic commands.
bool site_comfort_transaction_pending();
bool site_comfort_transaction_read(enevina::cottage::ComfortTxn& out);
bool site_comfort_transaction_abandon();
bool site_comfort_transaction_finish(const enevina::cottage::CottageModeConfig& config);
// The site write itself is not a commit proof; durably mark it after readback.
bool site_comfort_transaction_mark_site_committed();
// Only call with a physically validated, NVS-verified air mode image.
bool site_comfort_transaction_abort_verified(const enevina::cottage::CottageModeConfig& proven_air);
// TOTAL EUR/kWh for one slot using the CURRENT settings (single price rule).
bool site_slot_total_price(int64_t slot_start_utc,double spot_eur_per_kwh,double& total);
