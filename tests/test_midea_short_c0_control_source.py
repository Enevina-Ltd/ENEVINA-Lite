"""Short-C0 control retains its manual authority, freshness and queue contract.

Wire-default/readback behavior is executed in the C++ protocol/transport tests;
it must follow the pinned integrations instead of asserting a blanket refusal.
"""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
PROTO=(ROOT/"pump/midea_v3_protocol.cpp").read_text()
TCP=(ROOT/"firmware/cottage/src/cottage_midea.cpp").read_text()
SERVICE=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
class ShortC0ControlTests(unittest.TestCase):
 def test_write_stabilization_and_explicit_manual_pilot_unchanged(self):
  write=TCP.split("ReadStatus write_model(",1)[1].split("} // namespace",1)[0]
  self.assertIn("vTaskDelay(pdMS_TO_TICKS(1100))",TCP)
  self.assertIn("ac_control_confirmed(original,fresh,patch)",write)
  self.assertIn("ac_set_state_command(original,patch,2,control)",write)
  self.assertIn('web.on("/api/midea/pilot",HTTP_POST',SERVICE)
  self.assertIn("pilot_matches",SERVICE)
  self.assertIn("previous>0 && now>=previous && now-previous<=90",SERVICE)
  self.assertIn("action?2500u:300u",SERVICE)
  self.assertIn("deferred_for_budget",SERVICE)
  self.assertIn("control_pending.store(true);",SERVICE)
  self.assertIn("at-issued<14",SERVICE)
  self.assertIn("expected_link==cottage_wifi_link_generation()",SERVICE)
  self.assertIn("deferred_for_budget?500u:30000u",SERVICE)
  self.assertIn("after_lease-queued<15",SERVICE)
  self.assertIn("expected_link,expected_endpoint,before,queued+15,min_c,max_c",SERVICE)
  self.assertIn("command_window_open(",write)
  self.assertIn("site_settings_endpoint_generation()==expected_endpoint_generation",write)
if __name__=="__main__":unittest.main()
