#pragma once
// Phase-2 browser configuration persisted in NVS ("site" namespace).
// Compile-time COTTAGE_* values remain the fallback for an unconfigured
// device, so the existing pilot build keeps working unchanged.
#include "cottage/site_settings.hpp"
class WebServer;

void site_settings_initialize();             // once in setup(), before workers
enevina::cottage::SiteSettings site_settings_snapshot(); // thread-safe copy
uint32_t site_settings_generation();         // increments on every commit
// Increments only when the pump identity changes (CN105 URL or adapter).
// Armed CN105 control is bound to the identity it was enabled for.
uint32_t site_settings_endpoint_generation();
void site_settings_register_routes(WebServer& web);
// TOTAL EUR/kWh for one slot using the CURRENT settings (single price rule).
bool site_slot_total_price(int64_t slot_start_utc,double spot_eur_per_kwh,double& total);
