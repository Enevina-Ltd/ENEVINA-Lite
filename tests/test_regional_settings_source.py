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


if __name__ == "__main__":
    unittest.main()
