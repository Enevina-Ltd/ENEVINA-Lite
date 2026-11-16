"""Source-level C2 fail-closed review. This is NOT a hardware command proof."""
import pathlib
import re
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]

class ShadowSurfaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.main = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf8")
        cls.config = (ROOT / "firmware/cottage/include/cottage_config.hpp").read_text(encoding="utf8")

    def test_no_actuator_command_routes(self):
        self.assertNotRegex(self.main, r"HTTP_(?:POST|PUT|DELETE|PATCH)")
        self.assertNotRegex(self.main, r"(?:\.POST|\.PUT|\.PATCH|\.DELETE)\s*\(")
        self.assertNotRegex(self.main, r"rpc/(?:Switch\.Set|Switch\.Toggle|Switch\.SetConfig)")
        self.assertNotIn("setClimate", self.main)
        self.assertNotIn("setSetpoint", self.main)

    def test_only_read_only_http_requests(self):
        self.assertIn('request.GET()', self.main)
        self.assertIn("valid_cn105_events_url(COTTAGE_CN105_URL)", self.main)
        self.assertIn("valid_shelly_base_url(COTTAGE_SHELLY_URL)", self.main)
        self.assertIn('Switch.GetStatus?id=0', self.main)
        self.assertIn("HTTPC_DISABLE_FOLLOW_REDIRECTS", self.main)

    def test_no_production_calibration_bypass(self):
        self.assertRegex(self.config, r"#define COTTAGE_SYNTHETIC_TEST_MODEL 0")
        self.assertIn("COTTAGE_SYNTHETIC_TEST_MODEL && valid(model)", self.main)

    def test_n16r8_psram_is_initialized_and_checked(self):
        pio=(ROOT / "firmware/cottage/platformio.ini").read_text(encoding="utf8")
        self.assertIn("-DBOARD_HAS_PSRAM",pio)
        self.assertIn("C2_SHADOW_FATAL",self.main)
        self.assertIn("ESP.getPsramSize()",self.main)

    def test_firmware_rejects_invalid_clock_and_stale_measurements(self):
        self.assertIn("clock_ok()", self.main)
        self.assertIn("valid_cn105(cp,now)", self.main)
        self.assertIn("valid_temperature(ts,now)", self.main)
        self.assertIn("UnalignedStart", (ROOT / "cottage/forecast_bridge.hpp").read_text(encoding="utf8"))

if __name__=="__main__":
    unittest.main()
