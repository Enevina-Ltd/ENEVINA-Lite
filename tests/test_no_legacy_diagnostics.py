"""Single premium UI; no diagnostics route or duplicate HTML."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()
UI=(ROOT/"firmware/cottage/include/cottage_premium_dashboard.hpp").read_text()
AIR=(ROOT/"firmware/cottage/src/cottage_air_service.cpp").read_text()

class SingleDashboardTests(unittest.TestCase):
    def test_legacy_ui_and_route_are_gone(self):
        self.assertNotIn('static const char html[] PROGMEM',MAIN)
        self.assertNotIn('id="air-panel"',MAIN)
        self.assertIn('web.send_P(200,"text/html; charset=utf-8",premium_dashboard_html)',MAIN)
        self.assertNotIn('web.on("/diagnostics"',MAIN)
        self.assertNotIn('web.sendHeader("Location","/#settings")',MAIN)
        self.assertNotIn('href="/diagnostics"',UI)
        self.assertIn('href="/#settings"',UI)
        self.assertIn("window.addEventListener('hashchange'",UI)

    def test_critical_settings_transferred_to_premium(self):
        for api in ('/api/air/config','/api/air/thresholds',
                    '/api/air/bind','/api/air/ack','/api/air/history'):
            self.assertIn(api,AIR)
        for endpoint in ('/api/air/thresholds','/api/air/bind','/api/air/ack'):
            self.assertIn(endpoint,UI)
        for key in ('id="ruuvi-role"','id="ruuvi-mac"','id="ruuvi-bind"',
                    'id="alert-rh70"','id="alert-rh80"',
                    'id="alert-battery"','id="alerts-save"',
                    'id="alerts-active"'):
            self.assertIn(key,UI)
        self.assertIn("state.alertSettingsLoaded",UI)
        self.assertIn('a.bound_mac',UI)
        self.assertIn('a.discovered_mac',UI)
        self.assertIn("item.ack",UI)

if __name__=="__main__":
    unittest.main()
