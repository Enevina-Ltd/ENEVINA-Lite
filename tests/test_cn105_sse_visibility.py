"""Regression: visible legacy ESPHome SSE without inventing pump UART freshness."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
PREMIUM = (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")


class Cn105SseVisibilityTests(unittest.TestCase):
    def test_legacy_sse_values_survive_as_observed_only(self):
        ingest = MAIN.split("static void parse_sse_state(", 1)[1].split("static void sse_worker(", 1)[0]
        self.assertIn("copy=sse_observed_pump", ingest)
        self.assertIn("sse_observed_pump=copy;", ingest)
        self.assertIn("copy.captured_utc=0;", ingest)
        self.assertIn("copy.received=false;", ingest)
        self.assertIn("copy.evidence={now,0,false};", ingest)
        self.assertNotIn("copy.evidence={now,now,true}", ingest)
        self.assertNotIn("copy.captured_utc=now", ingest)
        self.assertIn('climate/Mitsubishi Electric MSZ-FD25VA', ingest)
        self.assertIn('sensor/Compressor Frequency', ingest)

    def test_status_tells_receive_age_not_uart_sample_age(self):
        status = MAIN.split("static void status_endpoint() {", 1)[1].split("static void plan_endpoint()", 1)[0]
        self.assertIn("observed=sse_observed_pump", status)
        self.assertIn("cp.evidence.source_time_verified", status)
        self.assertIn('cJSON_AddBoolToObject(root,"cn105_source_verified",verified_uart)', status)
        self.assertIn('cJSON_AddBoolToObject(root,"cn105_sse_climate_recent",', status)
        self.assertIn('observed_climate && sse_online && now-climate_time<=180', status)
        self.assertIn('observed_compressor && sse_online && now-compressor_time<=180', status)
        for k in ("cn105_sse_indoor_c", "cn105_sse_setpoint_c", "cn105_sse_mode",
                  "cn105_sse_compressor_hz", "cn105_sse_climate_age_s", "cn105_sse_compressor_age_s"):
            self.assertIn(f'"{k}"',status)
        self.assertIn('"SSE_SNAPSHOT_UART_UNVERIFIED"',status)
        self.assertIn("valid_cn105(cp,now)",status)
        self.assertRegex(status,r'if\(valid_cn105\(cp,now\)\).*?\{')

    def test_dashboard_labels_observed_values_and_age(self):
        self.assertIn("Mitsubishi CN105 · vastaanotettu data", PREMIUM)
        for e in ("mitsubishi-read-temp", "mitsubishi-read-mode", "mitsubishi-read-setpoint",
                  "mitsubishi-read-hz", "mitsubishi-compressor-age"):
            self.assertIn(f'id="{e}"',PREMIUM)
            self.assertIn(f"text('{e}'",PREMIUM)
        self.assertIn("ei UART-todistetta", PREMIUM)
        self.assertIn("ei todista uutta CN105-UART-lukua", PREMIUM)
        self.assertNotIn("s.cn105_sse_climate_recent===true?'UART-lähde", PREMIUM)

    def test_physical_commands_are_pilot_armed_not_source_gate_bypasses(self):
        # CN105 writes are now a separately armed path, not a promotion of SSE
        # into verified pump-origin telemetry or calibration.
        self.assertIn('static std::atomic<bool> control_armed{false}',MAIN)
        self.assertIn('cJSON_AddBoolToObject(root,"control_armed",control_armed.load())',MAIN)
        self.assertIn('cJSON_AddBoolToObject(root,"bootstrap_executable",false)',MAIN)
        self.assertIn("health.independent_freeze_protection_verified=false;",MAIN)
        self.assertIn("cn105_source_verifier.accept(p,verified)",MAIN)
        self.assertIn("cottage_ota_confirmation_pending()",MAIN)
        self.assertIn('c23_authorize_local_setting(web.arg("token").c_str())',MAIN)
        self.assertIn('control_armed.store(false)',MAIN)
        self.assertIn('if(prefs.getBool("armed",false))prefs.putBool("armed",false)',MAIN)
        post = MAIN.split("static bool cn105_bridge_post(",1)[1].split(
            "static void cottage_control_tick()",1)[0]
        self.assertIn('!control_armed.load()',post)
        # URL guard is applied at use time to the browser-configured value.
        self.assertIn('valid_cn105_events_url(settings.cn105_url)',MAIN)
        self.assertIn('HTTPC_DISABLE_FOLLOW_REDIRECTS',post)
        self.assertIn('req.POST("")',post)
        self.assertIn('req.addHeader("Content-Length","0")',post)
        self.assertLess(post.index('req.addHeader("Content-Length","0")'),
                        post.index('req.POST("")'))
        # ESPHome returns 411 for the old headerless empty POST. Ensure
        # the zero-length body header is kept next to the actual command.
        # GET acquires the nonrecursive network lease itself; it cannot be
        # called while the POST owns that same mutex.
        self.assertLess(post.index('cn105_bridge_get(bridge_heat,bridge_setpoint)'),
                        post.index('NetworkBudgetLease lease'))
        self.assertIn('const auto refreshed=active_control_proposal',post)
        self.assertIn('const auto decision=choose_mitsubishi_control(candidate);',MAIN)
        controller = MAIN.split("static bool cn105_bridge_post(",1)[1].split(
            "static void cottage_control_registration()",1)[0]
        self.assertNotIn("valid_cn105(", controller)
        self.assertNotIn("source_time_verified", controller)
        self.assertNotIn("cn105_source_proof_good", controller)
        self.assertIn("cn105_bridge_get", controller)
        self.assertIn("active_control_proposal", controller)
        self.assertIn("c23_config_generation()!=config_generation", controller)

        self.assertIn("confirmed=decision.step==ControlStep::Setpoint",MAIN)
        self.assertIn("if(!cn105_bridge_get(read_heating,read_setpoint))continue",MAIN)
        self.assertNotRegex(MAIN,r'Switch\\.(?:Set|Toggle|SetConfig)')



if __name__ == "__main__":
    unittest.main()
