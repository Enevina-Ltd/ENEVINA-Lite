"""Cross-check actual signed-OTA history proof and no-heat report wiring."""
from pathlib import Path
import unittest

R=Path(__file__).resolve().parents[1]
OTA=(R/"firmware/cottage/src/cottage_secure_ota.cpp").read_text(encoding="utf-8")
MAIN=(R/"firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
HISTORY=(R/"cottage/history_store.hpp").read_text(encoding="utf-8")
HISTORY_FW=(R/"firmware/cottage/src/cottage_history.cpp").read_text(encoding="utf-8")
POLICY=(R/"cottage/ota_history_policy.hpp").read_text(encoding="utf-8")
FRESH=(R/"cottage/data_freshness.hpp").read_text(encoding="utf-8")
DASH=(R/"firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")

class ConfirmedOtaHistoryAndNoHeat(unittest.TestCase):
    def test_confirmation_never_depends_on_optional_history_nvs(self):
        func=OTA.split("bool cottage_ota_confirm_boot() {",1)[1].split(
            "uint32_t cottage_ota_good_version()",1)[0]
        self.assertLess(func.index("ota::confirm_persistence(store,tx,good)"),
                        func.index("p.putUInt(kHistoryUpgradeKey,tx.version)"))
        self.assertIn("good>0 && tx.version>good",func)
        self.assertIn("last_history_upgrade.store(tx.version)",func)
        self.assertIn('p.getUInt(kHistoryUpgradeKey,0)==tx.version',func)
    def test_reboot_only_accepts_confirmed_good_version(self):
        init=OTA.split("void cottage_ota_initialize() {",1)[1].split(
            "bool cottage_ota_confirmation_pending()",1)[0]
        self.assertIn("marker>0 && marker==good?marker:0",init)
        self.assertIn("ota_pending_history_upgrade(",OTA)
        self.assertIn("cottage_ota_recorded_upgrade_version()",MAIN)
    def test_event_log_dedup_and_deferred_clock(self):
        self.assertIn("ota_upgrade_event_version",HISTORY)
        self.assertIn("confirmed_upgrade==version",HISTORY)
        self.assertIn("already_logged",HISTORY)
        self.assertIn("ota_pending_version_",HISTORY)
    def test_event_ack_only_after_durable_history_flush(self):
        self.assertIn('ota_pending_history_upgrade(',OTA)
        self.assertIn('ota_pending_history_upgrade(',POLICY)
        self.assertIn('cottage_ota_acknowledge_history_upgrade',OTA)
        self.assertIn('p.putUInt(kHistoryAckKey,version)',OTA)
        self.assertIn('kHistoryLastKey',OTA)
        self.assertIn('last_history_ack.store(has_summary && summary.version==good?',OTA)
        flush=HISTORY_FW.split('const bool ok=persistence->flush(*store,events)',1)[1]
        self.assertLess(flush.index('if(ok)'),flush.index('cottage_ota_acknowledge_history_upgrade(pending_ota,e.utc)'))
        self.assertIn('cottage_ota_acknowledge_history_upgrade(pending_ota,e.utc)',HISTORY_FW)
    def test_last_confirmed_update_is_persisted_outside_history_ring(self):
        self.assertIn('struct OtaHistoryLast',POLICY)
        self.assertIn('valid_ota_history_last',POLICY)
        self.assertIn('p.putBytes(kHistoryLastKey,&wanted,sizeof(wanted))',OTA)
        self.assertIn('p.getBytes(kHistoryLastKey,&readback,sizeof(readback))',OTA)
        self.assertIn('cottage_ota_last_successful_update(when,version)',MAIN)
        self.assertIn('last_history_version.store(wanted.version)',OTA)
        self.assertIn('last_history_utc.store(wanted.utc)',OTA)
    def test_no_heat_fallback_agrees_with_actual_action(self):
        self.assertIn("no_heating_requested=actual.action==BootstrapAction::Off",MAIN)
        self.assertIn("actual.reason==BootstrapReason::NoHeatingRequested",MAIN)
        self.assertIn("ControllerFallback::NoHeatingRequested",FRESH)
        self.assertIn("f.fallback==='no_heating_requested'",DASH)

if __name__=="__main__":
    unittest.main()
