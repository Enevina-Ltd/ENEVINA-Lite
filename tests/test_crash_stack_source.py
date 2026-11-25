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
    def test_timeline_and_bootstrap_do_not_nest_large_stack_copies(self):
        preview=MAIN.split('static BootstrapDecision bootstrap_preview(',1)[1].split('static void mpc_mode_target',1)[0]
        self.assertIn('site_region_total_price(site,points[n].start_utc',preview)
        self.assertNotIn('site_slot_total_price(points[n]',preview)
        self.assertNotIn('MaintenanceCycleReport cycle;',preview)
        timeline=MAIN.split('static void timeline_endpoint() {',1)[1].split('static void prices_endpoint()',1)[0]
        self.assertIn('heap_caps_malloc(kChunk,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)',timeline)
        self.assertIn('heap_caps_malloc(sizeof(SiteSettings)',timeline)
        self.assertIn('heap_caps_free(chunk);heap_caps_free(site_mem);',timeline)
        self.assertIn('site_region_total_price(site,points[i].start_utc',timeline)
        self.assertNotIn('char chunk[768]',timeline)
        self.assertNotIn('bootstrap_preview(now,indoor)',timeline)
        self.assertIn('meta.decision_price_valid=fallback_price_valid;',timeline)
        self.assertIn('if(fallback_price_valid)*fallback_price_valid=input.bootstrap.price_data_valid;',MAIN)
    def test_mpc_web_snapshot_lives_in_psram(self):
        endpoint=MAIN.split('static void mpc_endpoint() {',1)[1].split('static void weather_forecast_endpoint()',1)[0]
        self.assertIn('sizeof(MpcWebScratch),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',endpoint)
        self.assertIn('new(&scratch->input) ArbiterInput(build_arbiter_input(',endpoint)
        self.assertIn('scratch->calibration=calibration_snapshot;',endpoint)
        self.assertIn('scratch->record=mpc_plan;',endpoint)
        self.assertNotIn('const CalibrationReport cal=calibration_snapshot;',endpoint)
        self.assertNotIn('const MpcPlanRecord plan=mpc_plan;',endpoint)
        self.assertNotIn('confirmed_shared_meter_identity(site_settings_snapshot())',endpoint)
        self.assertIn('baseload_current(base,base_scope.shared,now)',endpoint)
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
