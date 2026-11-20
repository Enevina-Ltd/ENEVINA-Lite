"""A short Midea C0 must support the explicit setpoint pilot."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
PROTO=(ROOT/"pump/midea_v3_protocol.cpp").read_text()
TCP=(ROOT/"firmware/cottage/src/cottage_midea.cpp").read_text()
SERVICE=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
class ShortC0ControlTests(unittest.TestCase):
 def test_readback_retained_and_short_state_not_globally_rejected(self):
  setter=PROTO.split("bool set_state_command(",1)[1].split("bool control_patch_observed(",1)[0]
  readback=PROTO.split("bool preserved_state_observed(",1)[1].split("bool inner_encode(",1)[0]
  self.assertNotIn("!existing.freeze_protection_known",setter)
  self.assertIn("existing.freeze_protection?0x80:0",setter)
  self.assertIn("before.freeze_protection_known && after.freeze_protection_known",readback)
  self.assertIn("before.power!=after.power",readback)
  self.assertIn("before.mode!=after.mode",readback)
  self.assertIn("before.independent_aux_heat!=after.independent_aux_heat",readback)
 def test_write_stabilization_and_explicit_manual_pilot_unchanged(self):
  write=TCP.split("ReadStatus write_device(const char*",1)[1]
  self.assertIn("vTaskDelay(pdMS_TO_TICKS(1100))",write)
  self.assertIn("control_patch_observed(fresh,patch)",write)
  self.assertIn("preserved_state_observed(original,fresh,patch)",write)
  self.assertIn('web.on("/api/midea/pilot",HTTP_POST',SERVICE)
  self.assertIn("pilot_matches",SERVICE)
  self.assertIn("previous>0 && now>=previous && now-previous<=90",SERVICE)
  self.assertIn("action?2500u:300u",SERVICE)
  self.assertIn("deferred_for_budget",SERVICE)
  self.assertIn("control_pending.store(true);",SERVICE)
  self.assertIn("at-issued<14",SERVICE)
  self.assertIn("expected_link==cottage_wifi_link_generation()",SERVICE)
  self.assertIn("deferred_for_budget?500u:30000u",SERVICE)
if __name__=="__main__":unittest.main()
