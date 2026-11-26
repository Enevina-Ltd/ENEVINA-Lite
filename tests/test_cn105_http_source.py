"""Regression: timeout recovery must be read-only and bounded."""
from pathlib import Path
import unittest

SOURCE=(Path(__file__).resolve().parents[1]/
        "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
READ=SOURCE.split("static bool cn105_bridge_get(enevina::PumpRoomMode& observed_mode,double& target) {",1)[1].split(
    "static bool cn105_bridge_post(",1)[0]
POST=SOURCE.split("static bool cn105_bridge_post(",1)[1].split(
    "static bool control_current_image_sha256",1)[0]

class Cn105ReadRegression(unittest.TestCase):
    def test_two_bounded_read_attempts_only(self):
        self.assertIn("attempt<cn105_read_max_attempts",READ)
        self.assertIn("cn105_should_retry_read(code,attempt)",READ)
        self.assertIn("req.setTimeout(cn105_read_response_timeout_ms)",READ)
        self.assertIn("req.setConnectTimeout(cn105_read_connect_timeout_ms)",READ)
        self.assertIn("if(!lease.held())",READ)
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(cn105_read_retry_delay_ms))",READ)
        # A lease refusal retries *before* HTTPClient exists; that first
        # delay cannot require req.end(). For actual GET retries, the
        # HTTP request must be closed before the next wait.
        http=READ.split("WiFiClient client;",1)[1]
        self.assertLess(http.index("req.end();"),
                        http.index("vTaskDelay(pdMS_TO_TICKS(cn105_read_retry_delay_ms))"))
    def test_revalidate_source_identity_and_content(self):
        self.assertIn("strcmp(id->valuestring",READ)
        self.assertIn("mitsubishi_valid_setpoint(setpoint)",READ)
        self.assertIn("cJSON_ParseWithOpts",READ)
        self.assertIn("if(verified)",READ)
    def test_never_retry_or_relax_writes(self):
        self.assertNotIn("cn105_read_max_attempts",POST)
        self.assertNotIn("cn105_get_retry_count",POST)
        self.assertEqual(POST.count("req.POST("),1)
        self.assertIn("control_armed.load()",POST)
        self.assertIn("c23_config_generation()!=config_generation",POST)
        self.assertIn("site_settings_generation()!=site_generation",POST)

if __name__=="__main__":
    unittest.main()
