from pathlib import Path
import unittest
BASE=Path(__file__).resolve().parents[1]
SITE=(BASE/"firmware/cottage/src/cottage_site_settings.cpp").read_text(encoding="utf-8")
POLICY=(BASE/"cottage/nvs_cache_reclaim_policy.hpp").read_text(encoding="utf-8")
class NvsSiteRecovery(unittest.TestCase):
    def test_settings_have_bounded_cache_recovery_only(self):
        self.assertIn("persist_with_optional_cache_reclaim",SITE)
        self.assertIn('Preferences p;',SITE)
        self.assertIn('p.putBytes("v3",&blob,sizeof(blob))',SITE)
        self.assertIn('nvs_erase_key(handle,key)',SITE)
        self.assertIn('nvs_commit(handle)==ESP_OK',SITE)
        self.assertIn('{"enevina_weather","weather_v2"}',POLICY)
        self.assertIn('{"enevina_market","price_v2"}',POLICY)
        self.assertIn('{"enevina_market","price_reg_v2"}',POLICY)
    def test_never_touch_critical_nvs_keys(self):
        for key in ('"cred1"','"good"','"bad"','"pending"','"txv2"','"intent1"','"hourly_v1"'):
            self.assertNotIn(key,POLICY)
        self.assertNotIn("nvs_flash_erase",SITE)
        self.assertNotIn('p.clear()',SITE)
    def test_fallback_only_after_failure(self):
        self.assertIn('if(store()) return true;',POLICY)
        self.assertIn('if(erase(cache.nspace,cache.key) && store())return true;',POLICY)
if __name__=="__main__":unittest.main()
