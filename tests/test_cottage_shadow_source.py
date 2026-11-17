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

    def test_bridge_heartbeat_does_not_refresh_pump_data(self):
        link = self.main.split('sensor/CN105 Connection Uptime',1)[1].split('cJSON_Delete(root);',1)[0]
        self.assertIn("cn105_link_event_utc=now",link)
        self.assertNotIn("copy.captured_utc",link)
        self.assertNotIn("pump=copy",link)
        self.assertIn("copy.captured_utc=climate_stamp&&compressor_stamp",self.main)

    def test_one_shot_reset_reason_is_reported_without_new_watchdog(self):
        setup = self.main.split("void setup() {", 1)[1].split("void loop() {", 1)[0]
        status = self.main.split("static void status_endpoint() {", 1)[1].split("static void plan_endpoint()", 1)[0] if "static void plan_endpoint()" in self.main else self.main
        self.assertIn("boot_reset_reason_code=static_cast<int>(esp_reset_reason());", setup)
        self.assertIn("ENEVINA_LITE_BOOT reset_reason_code=", setup)
        self.assertIn('cJSON_AddNumberToObject(root,"boot_reset_reason_code",boot_reset_reason_code)', status)
        self.assertEqual(self.main.count("esp_reset_reason()"), 1)

    def test_cn105_source_envelope_needs_proof_not_heartbeat(self):
        self.assertIn("text_sensor/Enevina CN105 Verified Snapshot", self.main)
        self.assertIn("cn105_source_verifier.accept(p,verified)", self.main)
        self.assertIn("cn105_source_verifier.reconnect()", self.main)
        self.assertIn("pump.evidence.source_time_verified=false;", self.main)
        self.assertIn('cn105_proven_uart_polls', self.main)

    def test_c22_diagnostics_do_not_change_shadow_permissions(self):
        for marker in ("sse_connections","sse_disconnects","shelly_reads_ok",
                       "cn105_climate_utc","cn105_compressor_utc",
                       "market_last_fetch_utc","weather_last_fetch_utc",
                       "independent_freeze_protection_verified", "safety_reason"):
            self.assertIn(marker,self.main)
        self.assertIn('cJSON_AddBoolToObject(root,"physical_writes",false)',self.main)
        self.assertIn('health.independent_freeze_protection_verified=false;',self.main)

if __name__=="__main__":
    unittest.main()
