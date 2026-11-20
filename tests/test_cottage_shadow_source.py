from pathlib import Path
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

    def test_no_shelly_writes_and_explicit_cn105_arm_gate(self):
        # This regression intentionally replaced the obsolete C2 rule
        # forbidding all writes. New F08 limits a single local pump endpoint.
        self.assertNotRegex(self.main,r"rpc/(?:Switch\\.Set|Switch\\.Toggle|Switch\\.SetConfig)")
        self.assertNotIn("setClimate",self.main)
        self.assertNotIn("setSetpoint",self.main)
        self.assertIn('static std::atomic<bool> control_armed{false}',self.main)
        self.assertIn('web.on("/api/control/arm",HTTP_POST',self.main)
        self.assertIn('c23_authorize_local_setting(web.arg("token").c_str())',self.main)
        self.assertIn('control_armed.store(false)',self.main)
        # Old always-disarm assertion predates the signed-image-bound intent
        # record. A same-image reboot may restore the owner choice, but an OTA
        # image change or mismatching pump identity must fail closed.
        reg=self.main.split('static void cottage_control_registration() {',1)[1].split(
            'web.on("/api/control/arm"',1)[0]
        self.assertIn('control_armed.store(false)',reg)
        self.assertIn('valid_image && !activating_ota',reg)
        self.assertIn('control_intent_restorable(saved,site_settings_snapshot()',reg)
        self.assertIn('control_armed.store(restore && !must_revoke)',reg)
        self.assertIn('mode.mode==CottageMode::Drying',self.main)
        self.assertIn('valid_temperature(sample,now)',self.main)
        self.assertIn('control_last_bridge_read_utc',self.main)


    def test_only_read_only_http_requests(self):
        self.assertIn('request.GET()', self.main)
        # Phase-2 settings: same allow-list guard at use time AND at input time.
        self.assertIn("valid_cn105_events_url(settings.cn105_url)", self.main)
        self.assertIn("valid_shelly_base_url(settings.shelly_url)", self.main)
        settings=(Path(__file__).resolve().parents[1]/"cottage"/"site_settings.hpp").read_text()
        self.assertIn("valid_cn105_events_url(value)", settings)
        self.assertIn("valid_shelly_base_url(value)", settings)
        self.assertIn('Switch.GetStatus?id=0', self.main)
        self.assertIn("HTTPC_DISABLE_FOLLOW_REDIRECTS", self.main)

    def test_no_production_calibration_bypass(self):
        # The synthetic bench model stays off and never reaches control. The
        # only MPC proposal path is a plan from the measured-data identifier
        # whose calibration state is CALIBRATED (walk-forward validated).
        self.assertRegex(self.config, r"#define COTTAGE_SYNTHETIC_TEST_MODEL 0")
        self.assertIn("in.calibrated=calibration_snapshot.state==CalibrationState::Calibrated;", self.main)
        self.assertIn("const PlannerOutput po=plan_mpc(pi,rows);", self.main)
        self.assertIn("pi.identifier=id;", self.main)
        worker = self.main.split("static void optimizer_worker(void*) {", 1)[1].split(
            "static void status_endpoint()", 1)[0]
        self.assertNotIn("optimize(priced_model", worker)
        self.assertNotIn("setup_model", worker)

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
        self.assertIn("copy.captured_utc=0;",self.main)
        self.assertIn("copy.received=false;",self.main)
        self.assertIn('cp.evidence.source_time_verified',self.main)
        self.assertIn('cJSON_AddBoolToObject(root,"cn105_source_verified",verified_uart)',self.main)
        self.assertIn('"SSE_SNAPSHOT_UART_UNVERIFIED"',self.main)
        self.assertIn('"UNVERIFIED_SOURCE"',self.main)

    def test_replayed_sse_snapshot_is_not_promoted_to_verified_pump_data(self):
        section=self.main.split("static void parse_sse_state",1)[1].split("static void sse_worker",1)[0]
        # SSE receipt time is preserved for diagnostic counters, but never
        # copied into the CN105 sample's pump-origin freshness fields.
        self.assertIn("climate_utc=climate_stamp",section)
        self.assertIn("compressor_utc=compressor_stamp",section)
        self.assertIn("copy.captured_utc=0;",section)
        self.assertIn("copy.received=false;",section)
        self.assertNotIn("copy.captured_utc=climate_stamp",section)
        self.assertNotIn("copy.received=(copy.captured_utc",section)
        ui=self.main.split("static void status_endpoint()",1)[1]
        self.assertIn('cn105_source_verified',ui)
        self.assertIn('cn105_measurement_quality',ui)

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
        self.assertIn('cJSON_AddBoolToObject(root,"physical_writes",control_armed.load())',self.main)
        self.assertIn('cJSON_AddBoolToObject(root,"control_armed",control_armed.load())',self.main)
        self.assertIn('health.independent_freeze_protection_verified=false;',self.main)

if __name__=="__main__":
    unittest.main()
