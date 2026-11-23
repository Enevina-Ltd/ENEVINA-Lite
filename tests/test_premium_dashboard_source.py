"""C2.5 premium dashboard: source safety checks (no device required).

These checks do not replace PlatformIO, browser, or simultaneous ESP32-S3
Wi-Fi/BLE/HTTPS/MPC physical testing.
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
UI = (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")

class PremiumDashboardSourceTests(unittest.TestCase):
    def test_flash_backed_local_dashboard(self):
        self.assertIn("premium_dashboard_html[] PROGMEM", UI)
        self.assertIn('web.send_P(200,"text/html; charset=utf-8",premium_dashboard_html)', MAIN)
        self.assertNotIn('web.on("/diagnostics"', MAIN)
        self.assertNotIn('static const char html[] PROGMEM', MAIN)
        self.assertNotIn('web.send(200,"text/html; charset=utf-8",html)', MAIN)
        self.assertIn('id="ruuvi-bind"', UI)
        self.assertIn('id="alerts-save"', UI)
        self.assertIn('data-theme="night"', UI)
        self.assertIn("Europe/Helsinki", UI)
        self.assertIn("applyAutomaticNightTheme", UI)
        self.assertIn('id="energy-grade"', UI)
        self.assertIn('Ei virallinen energiatodistus tai E-luku', UI)
        self.assertIn('function ownEnergyClass(', UI)
        self.assertIn('Syötä todellinen 12 kuukauden kulutus', UI)
        self.assertNotIn('s.shelly_total_wh/8760',UI)

    def test_ui_has_no_external_assets_or_mock_sensor_numbers(self):
        # Attribution links and local LAN URL input placeholders are
        # not external assets. Never load remote JS, CSS, images or frames.
        self.assertNotRegex(UI, r'<(?:script|link|img|iframe)\b[^>]*\b(?:src|href)=["\']https?://')
        self.assertNotRegex(UI, r"<script\s+src=")
        self.assertIn('rel="noopener noreferrer"',UI)
        self.assertIn("http://www.w3.org/2000/svg",UI)
        self.assertIn("OHJAUS POIS", UI)
        self.assertIn("control-armed" if False else "control-enable", UI)
        self.assertIn("pilot-state-heading", UI)
        self.assertIn("Ei tuoretta mittausta", UI)

    def test_read_only_market_endpoint(self):
        # Price API is read-only and requires trusted home LAN.
        self.assertIn('web.on("/api/prices",HTTP_GET,[](){',MAIN)
        self.assertIn("if(cottage_wifi_require_home_lan(web))prices_endpoint();",MAIN)
        self.assertNotIn('web.on("/api/prices",HTTP_POST',MAIN)
        self.assertIn("market.clock_trusted || !market.current_price_valid", MAIN)
        self.assertIn("enevina_firmware::future_prices(points,kMaxPriceSlots)", MAIN)
        self.assertIn("MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT", MAIN)
        # One DST-safe price rule (audit 18.11.2026): /api/prices, /api/status,
        # bootstrap and MPC bridge all use site_slot_total_price; no TZ-based
        # localtime_r() tariff decisions remain in main.cpp.
        self.assertIn("site_slot_total_price(price.start_utc,", MAIN)
        # Price timeline is assembled by the current plan helpers;
        # price reads and UI do not dispatch commands.
        self.assertIn("site_slot_total_price(price.start_utc,", MAIN)
        self.assertIn("static void prices_endpoint()",MAIN)
        self.assertNotIn("localtime_r(&utc,&local)", MAIN)
        self.assertNotIn("tariff(local.tm_hour", MAIN)
        self.assertNotRegex(
            MAIN[MAIN.index("static void prices_endpoint()"):MAIN.index("static void setup_model()")],
            r"(Switch\.Set|CN105.*write|send.*command|relay_?off|relay_?on\s*\()",
        )

    def test_plan_only_if_backend_accepts_it(self):
        self.assertIn("p.valid===true", UI)
        self.assertIn("Ei hyväksyttyä suunnitelmaa", UI)
        self.assertIn("COTTAGE_SYNTHETIC_TEST_MODEL", MAIN)
        self.assertIn("physical_writes", MAIN)

    def test_local_settings_are_explicitly_not_actuators(self):
        self.assertIn("new URLSearchParams({token:a.config_token", UI)
        self.assertIn("'/api/air/config'", UI)
        self.assertIn("Lämpöpumpulle ei lähetetty käskyä", UI)
        self.assertNotIn("'/api/actuate'", UI)
        self.assertNotIn("'/rpc/Switch.Set'", UI)

if __name__ == "__main__":
    unittest.main()
