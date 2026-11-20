"""Regression for Codex: partial Preferences writes must not masquerade as rollback."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "firmware/cottage/src/cottage_air_service.cpp").read_text(encoding="utf-8")
CTRL = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
UI = (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")

class AirPartialNvsRecovery(unittest.TestCase):
    def test_save_failure_requires_flash_readback_before_reporting_old_target(self):
        self.assertIn('bool persisted_mode_matches(const CottageModeConfig& cfg)', SRC)
        self.assertIn('p.getFloat("target",NAN)', SRC)
        self.assertIn('p.getFloat("occupied",NAN)', SRC)
        self.assertIn('p.getFloat("dryadd",NAN)', SRC)
        endpoint=SRC.split('web.on("/api/air/config",HTTP_POST',1)[1].split('web.on("/api/air/thresholds"',1)[0]
        self.assertIn('const bool desired_durable=persisted_mode_matches(desired);',endpoint)
        self.assertIn('old_durable=persisted_mode_matches(previous);',endpoint)
        self.assertIn('save_config();',endpoint)
        self.assertIn('old_durable=persisted_mode_matches(previous);',endpoint)
        self.assertIn('config_storage_verified=false;',endpoint)
        self.assertIn('"durable_state"', endpoint.replace('\\', ''))
    def test_control_and_ui_refuse_unverified_ram_rollback(self):
        self.assertIn('c23_config_storage_verified()',CTRL)
        self.assertIn('cJSON_AddBoolToObject(air,"config_storage_verified",config_storage_verified);', SRC)
        self.assertIn('observed.config_storage_verified===false',UI)
        self.assertIn('(target status unverified)',UI)
        wizard=UI.split('async function wzSaveComfort(skip){',1)[1].split('function wzStepDevices(',1)[0]
        self.assertIn("confirmedStatus=await fetchJSON('/api/status')",wizard)
        self.assertIn('const observed=confirmedStatus&&confirmedStatus.air;',wizard)
        self.assertNotIn('const observed=state.status&&state.status.air;',wizard)

if __name__=="__main__":
    unittest.main()
