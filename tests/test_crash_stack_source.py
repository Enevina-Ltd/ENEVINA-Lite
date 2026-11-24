"""ESP stack regressions: no aggregate copies of the model/horizon on the task stack."""
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/'firmware/cottage/src/main.cpp').read_text()
IDENT=(ROOT/'cottage/building_identification.cpp').read_text()
SETTINGS=(ROOT/'firmware/cottage/src/cottage_site_settings.cpp').read_text()
class CrashStackSource(unittest.TestCase):
    def test_inplace_model_and_horizon(self):
        self.assertIn('join_forecast_horizon_into(*hz,now',MAIN)
        self.assertNotIn('*hz=HorizonResult{}',MAIN)
        self.assertNotIn('*hz=join_forecast_horizon(',MAIN)
        self.assertNotIn('*this=BuildingIdentifier(NoInit{})',IDENT)
        self.assertNotIn('BuildingIdentifier x(variable_setpoints_)',IDENT)
        self.assertIn('new(this) BuildingIdentifier(variable)',IDENT)
    def test_restore_scratch_psram_only(self):
        setup=SETTINGS.split('void site_settings_initialize()',1)[1].split('SiteSettings site_settings_snapshot()',1)[0]
        self.assertIn('heap_caps_malloc(sizeof(RestoreScratch),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)',setup)
        self.assertIn('auto& blob=scratch->blob',setup)
        self.assertIn('auto& raw4=scratch->raw4',setup)
        self.assertIn('auto& raw3=scratch->raw3',setup)
        self.assertIn('auto& raw2=scratch->raw2',setup)
    def test_only_readonly_summary_not_raw_core(self):
        self.assertEqual(MAIN.count('capture_boot_crash();'),1)
        self.assertIn('esp_core_dump_image_check()',MAIN)
        self.assertIn('esp_core_dump_get_summary(&dump)',MAIN)
        self.assertNotIn('esp_core_dump_image_erase(',MAIN)
        self.assertNotIn('dump.ex_info.exc_a',MAIN)
        self.assertNotIn('esp_core_dump_image_get(',MAIN)
        self.assertIn('web.on("/api/diagnostics/crash",HTTP_GET',MAIN)
        self.assertIn('if(cottage_wifi_require_home_lan(web))crash_evidence_endpoint()',MAIN)
        self.assertIn('crashed_elf_sha',MAIN)
if __name__=='__main__': unittest.main()
