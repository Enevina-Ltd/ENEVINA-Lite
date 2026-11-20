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
    def test_comfort_transaction_is_durable_before_site_and_completed_only_after_air(self):
        site=(ROOT / "firmware/cottage/src/cottage_site_settings.cpp").read_text(encoding="utf-8")
        header=(ROOT / "cottage/comfort_transaction.hpp").read_text(encoding="utf-8")
        self.assertIn('comfort_pending.store(p.isKey(kComfortKey))',site)
        self.assertIn('bool prepare_comfort_transaction(',site)
        endpoint=site.split('web.on("/api/settings",HTTP_POST',1)[1]
        self.assertLess(endpoint.index('prepare_comfort_transaction(comfort_tx)'),
                        endpoint.index('const bool site_saved=persist(next) && site_persisted_matches(next);'))
        self.assertIn('site_comfort_transaction_finish(desired)',SRC)
        self.assertIn('site_comfort_transaction_mark_site_committed()',site)
        self.assertIn('site_comfort_transaction_abort_verified(air)',site)
        self.assertIn('const bool site_saved=persist(next) && site_persisted_matches(next)',site)
        self.assertIn('comfort_limits_fit_target(',site)
        self.assertIn('COMFORT_TARGET_CONFLICT',site)
        self.assertIn('COMFORT_FIELDS',site)
        self.assertIn('explicit_old_band',site)
        self.assertIn('mode_comfort_limits_fit(site_settings_snapshot(),proposed)',SRC)
        self.assertIn('if(!mode_comfort_limits_fit(site,mode))return {};',CTRL)
        self.assertIn('COMFORT_RECOVERY_REQUIRED',SRC)
        self.assertIn('tx.phase==static_cast<uint8_t>(ComfortPhase::Prepared)',header)
        self.assertIn('comfort_recovery_action(',SRC)
        self.assertIn('site_comfort_transaction_abort_verified(config)',SRC)
        self.assertIn('site_comfort_transaction_abort_verified(previous)',SRC)
        self.assertIn('comfort_old_site_matches',header)
        self.assertIn('t.phase==static_cast<uint8_t>(ComfortPhase::SiteCommitted)',header)
        init=SRC.split('void c23_initialize()',1)[1].split('void c23_ble_worker(',1)[0]
        self.assertIn('comfort_apply_target(tx,recovered)',init)
        self.assertIn('persisted_mode_matches(recovered)',init)
        self.assertIn('config_storage_verified=false;',init)
        self.assertIn('site_comfort_transaction_abort_verified(config)',init)
        self.assertIn('ComfortRecoveryAction::RestorePriorSite',init)
        self.assertIn('site_comfort_transaction_pending()',SRC)
        self.assertIn('valid_comfort_txn',header)
        wizard=UI.split('async function wzSaveComfort(skip){',1)[1].split('function wzStepDevices(',1)[0]
        self.assertIn('newFields.comfort_txn_target=String(r.target)',wizard)
        self.assertIn('newFields.comfort_txn_mode=String(mode)',wizard)
    def test_control_and_ui_refuse_unverified_ram_rollback(self):
        self.assertIn('c23_config_storage_verified()',CTRL)
        self.assertIn('config_storage_verified&&!site_comfort_transaction_pending()', SRC)
        self.assertIn('observed.config_storage_verified===false',UI)
        self.assertIn('(target status unverified)',UI)
        wizard=UI.split('async function wzSaveComfort(skip){',1)[1].split('function wzStepDevices(',1)[0]
        self.assertIn("confirmedStatus=await fetchJSON('/api/status')",wizard)
        self.assertIn('const observed=confirmedStatus&&confirmedStatus.air;',wizard)
        self.assertNotIn('const observed=state.status&&state.status.air;',wizard)

if __name__=="__main__":
    unittest.main()
