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


class RegionalWiring(unittest.TestCase):
    def test_market_reader_has_no_hardcoded_finnish_market_or_tz(self):
        self.assertNotIn("deliveryArea=FI", MARKET)
        self.assertNotIn("EET-2EEST", MARKET)
        self.assertNotIn("setenv(", MARKET)
        self.assertNotIn("localtime_r", MARKET)
        self.assertIn("nordpool_market_date(now,day", MARKET)
        self.assertIn("parse_day_ahead(body,used,area", MARKET)

    def test_region_change_invalidates_cache_and_inflight_fetch(self):
        self.assertIn("++region_generation", MARKET)
        self.assertIn("generation==region_generation", MARKET)
        self.assertIn('nvs_erase_key(handle,"price_v2")', MARKET)
        # Stamp is written after the blob so a torn write is rejected on load.
        self.assertLess(MARKET.index('nvs_set_blob(handle,"price_v2"'),
                        MARKET.index("nvs_set_str(handle,region_key,stamp)"))
        # No zone chosen = no fetch, never another country's prices.
        self.assertIn("market_set_region(nullptr,nullptr)", SITE)
        self.assertIn("if(!area[0])", MARKET)

    def test_settings_v2_migrates_v1_and_drops_it_after_durable_write(self):
        self.assertIn('p.putBytes("v2",&blob,sizeof(blob))', SITE)
        self.assertIn('p.remove("v1")', SITE)
        self.assertLess(SITE.index('p.putBytes("v2"'), SITE.index('p.remove("v1")'))
        self.assertIn("site_settings_from_v1(old.settings)", SITE)
        self.assertIn("apply_site_settings_batch", SITE)

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

    def test_live_controller_uses_profile_and_never_guesses(self):
        self.assertIn("apply_profile(input,site.profile,site.comfort_min_c", self.MAIN)
        # inconsistent settings -> no decision
        self.assertRegex(self.MAIN, r"site\.min_price_advantage_fraction,window\)\)\s*return \{\};")
        # a window shorter than 8 contiguous slots is never used
        self.assertIn("n>=kMinSlots", self.MAIN)
        self.assertIn("points[n].start_utc==start+static_cast<int64_t>(n)*900", self.MAIN)
        self.assertNotIn("constexpr size_t N=8;", self.MAIN)

    def test_profile_is_visible_in_status_and_never_arms_control(self):
        self.assertIn('"optimization_profile"', self.MAIN)
        self.assertNotIn("control_armed.store(true)", (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8"))


class DiscoveryWiring(unittest.TestCase):
    AIR = (ROOT / "firmware/cottage/src/cottage_air_service.cpp").read_text(encoding="utf-8")
    SHELLY = (ROOT / "firmware/cottage/src/cottage_shelly_discovery.cpp").read_text(encoding="utf-8")
    SITE = (ROOT / "firmware/cottage/src/cottage_site_settings.cpp").read_text(encoding="utf-8")

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
        self.assertIn("bootstrap_preview(now,c23_temperature_sample())", self.MAIN)
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


if __name__ == "__main__":
    unittest.main()
