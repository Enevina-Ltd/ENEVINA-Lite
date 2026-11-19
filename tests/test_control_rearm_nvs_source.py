from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/"firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
POL=(ROOT/"cottage/nvs_cache_reclaim_policy.hpp").read_text(encoding="utf-8")
class NvsRearm(unittest.TestCase):
 def test_only_disposable_caches_eligible_for_reclaim(self):
    self.assertIn('{"enevina_weather","weather_v2"}',POL)
    self.assertIn('{"enevina_market","price_v2"}',POL)
    self.assertNotIn('{"cottage_ctl"',POL)
    self.assertNotIn('{"c24_ota"',POL)
    self.assertNotIn('{"cottage_wifi"',POL)
 def test_user_arm_only_reclaim(self):
    self.assertIn("persist_with_optional_cache_reclaim(\n            store,control_erase_disposable_cache)",SRC)
    assert "static bool control_save_user_arm(" in SRC
    self.assertIn("const bool stored=allowed && control_save_user_arm(record);",SRC)
    # Startup and endpoint-migration revocation do not run reclaim.
    early=SRC.split("bool cottage_control_revoke_saved_intent()",1)[1].split("static void cottage_control_disarm()",1)[0]
    self.assertNotIn("control_save_user_arm",early)
    self.assertIn('prefs.putBytes("intent1",&record,sizeof(record))',early)
 def test_native_nvs_error_never_overwritten_by_cache_errors(self):
    self.assertIn('nvs_open("cottage_ctl",NVS_READWRITE,&handle)',SRC)
    self.assertIn('nvs_set_blob(handle,"intent1",&record,sizeof(record))',SRC)
    self.assertIn("if(err==ESP_OK)err=nvs_commit(handle);",SRC)
    self.assertIn("last_write_error=err;",SRC)
    self.assertIn("control_arm_nvs_last_error.store(last_write_error);",SRC)
 def test_fail_closed_and_operational_counters(self):
    self.assertIn("const bool stored=allowed && control_save_user_arm(record);",SRC)
    self.assertIn("if(stored) {",SRC)
    self.assertIn("control_armed.store(requested);",SRC)
    for marker in ('"control_arm_nvs_reclaimed"','"control_arm_nvs_save_failures"','"control_arm_nvs_last_error"'):
     self.assertIn(marker,SRC)
if __name__=="__main__":unittest.main()
