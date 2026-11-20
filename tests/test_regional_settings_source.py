"""Source-level guards for the regional price/location wiring.

The behaviour itself is covered by host tests (market_zones_tests,
site_settings_tests, geocode_tests); these checks only pin the firmware wiring
that cannot run on the host: that nothing falls back to a hard-coded Finnish
market or TZ, and that the web handler never performs the network request.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MARKET = (ROOT / "firmware/main/market.cpp").read_text(encoding="utf-8")
SITE = (ROOT / "firmware/cottage/src/cottage_site_settings.cpp").read_text(encoding="utf-8")
GEO = (ROOT / "firmware/cottage/src/cottage_geocode.cpp").read_text(encoding="utf-8")
ROOTS = (ROOT / "firmware/main/tls_roots.hpp").read_text(encoding="utf-8")
ROLLBACK_POLICY = (ROOT / "cottage/site_rollback_policy.hpp").read_text(encoding="utf-8")


class RegionalWiring(unittest.TestCase):
    def test_market_reader_has_no_hardcoded_finnish_market_or_tz(self):
        self.assertNotIn("deliveryArea=FI", MARKET)
        self.assertNotIn("EET-2EEST", MARKET)
        self.assertNotIn("setenv(", MARKET)
        self.assertNotIn("localtime_r", MARKET)
        self.assertIn("nordpool_market_date(now,day", MARKET)
        self.assertIn("parse_day_ahead(body,used,area", MARKET)

    def test_region_change_wakes_reader_and_scales_consumers(self):
        self.assertIn("xTaskNotifyGive(reader_task)", MARKET)
        self.assertIn("ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(next_ms))", MARKET)
        self.assertIn("price_limit_scale=currency_limit_scale(market.currency)", (ROOT / "firmware/cottage/src/main.cpp").read_text())
        # The live MPC planner receives the market currency scale (the model's
        # Parameters are built from it in BuildingIdentifier::build_parameters).
        self.assertIn("pi.currency_scale=currency_limit_scale(ms.currency)", (ROOT / "firmware/cottage/src/main.cpp").read_text())

    def test_region_change_invalidates_cache_and_inflight_fetch(self):
        self.assertIn("++region_generation", MARKET)
        self.assertIn("generation==region_generation", MARKET)
        self.assertIn('nvs_erase_key(handle,"price_v2")', MARKET)
        # Stamp is written after the blob so a torn write is rejected on load.
        self.assertLess(MARKET.index('nvs_set_blob(handle,cache_blob_key'),
                        MARKET.index("nvs_set_str(handle,region_key,stamp)"))
        # Rollback firmware reads price_v2 without a region stamp, so other
        # regions must use a distinct key rather than poison its price cache.
        self.assertIn('regional_cache_key[]="price_reg_v2"',MARKET)
        self.assertIn('legacy_fi?"price_v2":regional_cache_key',MARKET)
        self.assertIn('const char* obsolete_key=legacy_fi?regional_cache_key:"price_v2"',MARKET)
        self.assertIn('migrate_early_region',MARKET)
        # No zone chosen = no fetch, never another country's prices.
        self.assertIn("market_set_region(nullptr,nullptr)", SITE)
        self.assertIn("if(!area[0])", MARKET)

    def test_boot_adoption_never_erases_the_saved_price_cache(self):
        # Codex P1 (#55): the first region call at boot compared with the FI/EUR
        # defaults and erased valid prices of every non-Finnish device.
        body = MARKET.split("bool market_set_region(")[1]
        self.assertIn("region_adopted", MARKET)
        self.assertLess(body.index("if(!region_adopted)"), body.index('nvs_erase_key(handle,"price_v2")'))
        first = body.split("if(!region_adopted) {")[1].split("return true;")[0]
        self.assertNotIn("nvs_", first)
        self.assertNotIn("++region_generation", first)
        self.assertNotIn("cache=PriceCache{}", first)

    def test_settings_v5_preserves_v4_and_older_rollback_keys(self):
        self.assertIn('constexpr uint16_t kVersion=5;',SITE)
        self.assertIn('p.putBytes("v5",&blob,sizeof(blob))', SITE)
        self.assertIn('p.getBytesLength("v4")',SITE)
        self.assertIn('site_restore_v4_record(',SITE)
        self.assertIn('site_old_keys_reclaimable(',SITE)
        self.assertIn('state==ESP_OTA_IMG_VALID',SITE)
        self.assertIn('cottage_ota_confirmation_pending()',SITE)
        self.assertIn('cottage_ota_runtime_ready()',SITE)
        self.assertIn('running_image_valid && ota_service_initialized &&',ROLLBACK_POLICY)
        self.assertIn('!ota_confirmation_pending',ROLLBACK_POLICY)
        self.assertLess(SITE.index('p.putBytes("v5"'),SITE.index('p.remove("v1")'))
        self.assertIn('if(p.isKey("v4")) p.remove("v4")',SITE)
        self.assertIn('if(p.isKey("v3")) p.remove("v3")',SITE)
        self.assertIn('if(p.isKey("v2")) p.remove("v2")',SITE)
        for key in ("v2","v3","v4"):
            self.assertIn('!rollback_key_signature(p,"'+key+'",blob.rollback_'+key+'_signature)',SITE)
            self.assertLess(SITE.index('rollback_key_signature(p,"'+key+'",blob.rollback_'+key+'_signature)'), SITE.index('p.putBytes("v5"'))
        self.assertIn('snapshot_read_failed=true;',SITE)
        self.assertIn('return !snapshot_read_failed && erase_disposable_cache',SITE)
        self.assertIn('site_v3_prefer_rollback_v2(',SITE)
        self.assertIn('site_rollback_v2_modified(',SITE)
        self.assertIn("site_settings_from_v1(old.settings)",SITE)
        self.assertIn("apply_site_settings_batch",SITE)

    def test_unconfirmed_contract_cannot_drive_price_optimization(self):
        tariff=(ROOT / "cottage/site_settings.hpp").read_text()
        self.assertIn("if(!s.tariff_confirmed) return false;",tariff)

    def test_geocode_request_runs_in_worker_not_in_web_handler(self):
        self.assertIn("xTaskCreate(worker_main", GEO)
        self.assertIn("NetworkBudgetLease", GEO)
        self.assertIn("MALLOC_CAP_SPIRAM", GEO)
        self.assertIn("cfg.cert_pem=photon_ca_pem", GEO)
        self.assertNotIn("esp_http_client", SITE)
        self.assertIn("geocode_search(", SITE)
        self.assertIn("photon_ca_pem", ROOTS)
        self.assertIn("96:BC:EC:06:26:49:76:F3", ROOTS)  # documented fingerprint


class ProfileWiring(unittest.TestCase):
    MAIN = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")

    def test_saved_settings_survive_an_upgrade_by_prefix_restore(self):
        # Codex P1 (#57): the loader demanded sizeof(blob) exactly and reset the
        # owner's configuration whenever the layout had grown.
        self.assertNotIn('getBytesLength("v2")==sizeof(blob)', SITE)
        self.assertIn("restore_site_settings(", SITE)
        self.assertIn('getBytesLength("v2")', SITE)

    def test_live_controller_uses_profile_and_never_guesses(self):
        self.assertIn("apply_profile(input,site.profile,site.comfort_min_c", self.MAIN)
        # inconsistent settings -> no decision
        self.assertRegex(self.MAIN, r"site\.min_price_advantage_fraction,window\)\)\s*return \{\};")
        # a window shorter than 8 contiguous slots is never used
        self.assertIn("n>=kMinSlots", self.MAIN)
        self.assertIn("points[n].start_utc==start+static_cast<int64_t>(n)*900", self.MAIN)
        self.assertNotIn("constexpr size_t N=8;", self.MAIN)
        self.assertNotIn("input.min_price_advantage_fraction=site_settings_snapshot()",self.MAIN)

    def test_profile_is_visible_in_status_and_never_arms_control(self):
        self.assertIn('"optimization_profile"', self.MAIN)
        self.assertNotIn("control_armed.store(true)", (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8"))


class DiscoveryWiring(unittest.TestCase):
    AIR = (ROOT / "firmware/cottage/src/cottage_air_service.cpp").read_text(encoding="utf-8")
    SHELLY = (ROOT / "firmware/cottage/src/cottage_shelly_discovery.cpp").read_text(encoding="utf-8")
    SITE = (ROOT / "firmware/cottage/src/cottage_site_settings.cpp").read_text(encoding="utf-8")

    def test_finished_scan_results_are_kept_from_completion_not_start(self):
        # Codex P2 (#58): a scan slower than 8 s was "old" on the first poll after
        # it finished, so it was discarded and restarted in a loop.
        body = self.SHELLY.split("ShellyScanState shelly_scan(")[1]
        self.assertIn("shared.state==ShellyScanState::Done?finished_ms:started_ms", body)
        self.assertNotIn("now-started_ms<8000", body)

    def test_sensor_list_freshness_follows_accepted_measurements(self):
        # Codex P2 (#58): replayed frames refreshed the age and made a sensor "live".
        self.assertIn("e.has_measurement&&age_ms<=kRuuviFreshMs", self.AIR)
        self.assertIn("now-e.measured_ms", self.AIR)
        self.assertIn('"heard_age_s"', self.AIR)

    def test_ble_callback_feeds_registry_and_names_never_run_ahead_of_flash(self):
        self.assertIn("sensor_registry.observe(parsed,ms)", self.AIR)
        self.assertIn("if(!saved) sensor_names=previous;", self.AIR)
        self.assertIn("loaded.load(raw)", self.AIR)          # corrupt blob -> no names

    def test_shelly_scan_is_read_only_bounded_and_off_the_web_loop(self):
        self.assertIn("xTaskCreate(worker_main", self.SHELLY)
        self.assertIn("shelly_lan_base_url(octets", self.SHELLY)
        self.assertIn("parse_shelly_info(body", self.SHELLY)
        self.assertIn("size>0 && static_cast<size_t>(size)<cap", self.SHELLY)   # Content-Length bound
        for forbidden in ("Switch.Set", "Switch.Toggle", "Relay", "http.POST", "http.PUT"):
            self.assertNotIn(forbidden, self.SHELLY)
        self.assertNotIn("HTTPClient", self.SITE)               # handler only polls the worker
        self.assertIn("shelly_scan(*snap)", self.SITE)


class TimelineWiring(unittest.TestCase):
    MAIN = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
    BOOT = (ROOT / "cottage/price_bootstrap.hpp").read_text(encoding="utf-8")
    TL = (ROOT / "cottage/plan_timeline.hpp").read_text(encoding="utf-8")

    def test_endpoint_uses_the_controllers_profile_and_classifier(self):
        self.assertIn("timeline_setup(site.profile", self.MAIN)
        self.assertIn("build_timeline(utc,total,n,params,*timeline)", self.MAIN)
        self.assertIn("bootstrap_preview(now,indoor)", self.MAIN)
        # one classifier: the controller and the timeline share it
        self.assertIn("classify_price(in.next_total_prices", self.BOOT)
        self.assertIn("classify_price(total+i", self.TL)

    def test_timeline_never_invents_indoor_or_cost(self):
        for forbidden in ("indoor_c", "predicted", "cost_eur", "savings"):
            self.assertNotIn(forbidden, self.TL.split("timeline_json")[1])

    def test_endpoint_buffers_live_in_psram_not_internal_ram(self):
        body = self.MAIN.split("static void timeline_endpoint()")[1].split("static void prices_endpoint()")[0]
        self.assertGreaterEqual(body.count("MALLOC_CAP_SPIRAM"), 4)
        self.assertNotIn("cJSON_Create", body)

    def test_drying_heat_permission_and_action_are_proposals_not_live_pump_state(self):
        timeline = self.MAIN.split("static void timeline_endpoint()")[1].split("static void prices_endpoint()")[0]
        self.assertIn("meta.heating_permitted=has_target", timeline)
        self.assertIn("drying_blocks_heat(plan)", timeline)
        self.assertIn("meta.indoor_fresh=valid_temperature(indoor,now)", timeline)
        self.assertIn("has_target && meta.indoor_fresh", timeline)
        self.assertIn("bootstrap_preview(now,indoor)", timeline)
        self.assertIn('heating_permitted', self.TL)
        dashboard = (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")
        self.assertIn('heating_permitted===false', dashboard)
        self.assertIn("Ehdotus: lämmitys", dashboard)
        self.assertIn("Ehdotus: lämmitys pois", dashboard)


class HistoryWiring(unittest.TestCase):
    HIST = (ROOT / "firmware/cottage/src/cottage_history.cpp").read_text(encoding="utf-8")
    MAIN = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")

    def test_flash_use_is_bounded_and_isolated(self):
        self.assertIn('LittleFS.begin(false,"/lfs",5,"spiffs")', self.HIST)
        self.assertIn('LittleFS.begin(true,"/lfs",5,"spiffs")', self.HIST)   # only after the plain mount failed
        self.assertLess(self.HIST.index('LittleFS.begin(false'), self.HIST.index('LittleFS.begin(true'))
        self.assertIn("kFlushIntervalMs=60UL*60*1000", self.HIST)
        for forbidden in ("Preferences", "nvs_", "esp_ota", "nvs_flash_erase", "esp_partition_erase"):
            self.assertNotIn(forbidden, self.HIST)
        self.assertIn('LittleFS.rename(tmp_,path_)', self.HIST)               # atomic replace
        self.assertIn("MALLOC_CAP_SPIRAM", self.HIST)                          # ring in PSRAM, fail closed
        self.assertIn("feature disabled", self.HIST)

    def test_collector_records_only_fresh_values_and_flags_forecasts(self):
        body = self.MAIN.split("static enevina::cottage::HistoryInputs history_collect()")[1].split("static void history_worker")[0]
        self.assertIn("valid_temperature(te,now)", body)
        self.assertIn("valid_shelly(st,now)", body)
        self.assertIn("in.outdoor_is_forecast=true", body)
        self.assertIn("in.outdoor_is_forecast=false", body)
        self.assertIn("sse_fresh", body)
        self.assertIn("in.clock_trusted=clock_ok()", body)

    def test_setpoint_comes_from_the_verified_snapshot_and_scope_is_recorded(self):
        # Codex P2/P1 (#60): sse_observed_pump is never marked received, so the
        # setpoint history was always empty; and the meter scope must be stored
        # with each reading, not looked up from today's setting.
        body = self.MAIN.split("static enevina::cottage::HistoryInputs history_collect()")[1].split("static void history_worker")[0]
        self.assertIn("in.setpoint_valid=valid_cn105(cp,now);in.setpoint_c=cp.setpoint_c;", body)
        self.assertNotIn("in.setpoint_valid=sse_fresh", body)
        self.assertIn("in.power_scope=", body)

    def test_dashboard_defines_the_six_column_card(self):
        # Codex P2 (#60): only the mobile rule had .span6, so the two history cards
        # took one grid column each on tablet/desktop.
        dash = (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")
        self.assertIn(".span6{grid-column:span 6}", dash)

    def test_worker_is_separate_low_priority_and_not_in_health_gate(self):
        self.assertIn('xTaskCreatePinnedToCore(history_worker,"cottage_hist",8192,nullptr,1,nullptr,0)', self.MAIN)
        self.assertIn("enevina_firmware::history_initialize();", self.MAIN)


if __name__ == "__main__":
    unittest.main()
