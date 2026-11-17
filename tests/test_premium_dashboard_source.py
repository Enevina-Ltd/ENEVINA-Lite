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
        self.assertIn('web.on("/diagnostics",HTTP_GET', MAIN)
        self.assertIn('web.sendHeader("Location","/#settings")', MAIN)
        self.assertNotIn('static const char html[] PROGMEM', MAIN)
        self.assertNotIn('web.send(200,"text/html; charset=utf-8",html)', MAIN)
        self.assertIn('id="ruuvi-bind"', UI)
        self.assertIn('id="alerts-save"', UI)

    def test_ui_has_no_external_assets_or_mock_sensor_numbers(self):
        self.assertNotIn("https://", UI)
        self.assertNotIn("http://", UI.replace("http://www.w3.org/2000/svg", ""))
        self.assertNotRegex(UI, r"<script\s+src=")
        self.assertIn("OHJAUS POIS", UI)
        self.assertIn("control-armed" if False else "control-enable", UI)
        self.assertIn("pilot-state-heading", UI)
        self.assertIn("Ei tuoretta mittausta", UI)

    def test_read_only_market_endpoint(self):
        self.assertIn('web.on("/api/prices",HTTP_GET,prices_endpoint)', MAIN)
        self.assertIn("market.clock_trusted || !market.current_price_valid", MAIN)
        self.assertIn("enevina_firmware::future_prices(points,kMaxPriceSlots)", MAIN)
        self.assertIn("MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT", MAIN)
        self.assertIn("calculate_total_price(t,total)", MAIN)
        self.assertIn("localtime_r(&utc,&local)", MAIN)
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
