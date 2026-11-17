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

    def test_no_real_pump_writes_or_source_gate_bypass(self):
        self.assertIn('cJSON_AddBoolToObject(root,"physical_writes",false)',MAIN)
        self.assertIn("health.independent_freeze_protection_verified=false;",MAIN)
        self.assertNotRegex(MAIN,r'HTTP_(?:POST|PUT|PATCH|DELETE)')
        self.assertNotRegex(MAIN,r'\.POST\s*\(')
        self.assertIn("cn105_source_verifier.accept(p,verified)",MAIN)
        self.assertIn("cottage_ota_confirmation_pending()",MAIN)


if __name__ == "__main__":
    unittest.main()
