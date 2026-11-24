"""Regression of the exact OTA WDT/memory state machine source."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
OTA=(ROOT/"firmware/cottage/src/cottage_secure_ota.cpp").read_text()
APP=(ROOT/"firmware/cottage/src/main.cpp").read_text()
WORKER=OTA.split("void cottage_ota_worker(void*) {",1)[1]
UPLOAD=OTA.split("bool update_app(const Release& release) {",1)[1].split(
    "#endif // ENABLED",1)[0]
class OtaRuntimeRegression(unittest.TestCase):
    def test_sequential_flash_erase_and_real_idle_time(self):
        self.assertIn("esp_ota_begin(target,OTA_WITH_SEQUENTIAL_WRITES,&handle)",UPLOAD)
        self.assertNotIn("        taskYIELD();",UPLOAD.split("while(done<release.size) {",1)[1].split("image.reset()",1)[0])
        self.assertIn("vTaskDelay(enevina::cottage::ota_flash_idle_delay_ticks)",UPLOAD)
        self.assertIn("ota_programmed_bytes.store(done)",UPLOAD)
        self.assertIn("ota_written_bytes.store(static_cast<uint32_t>(done))",OTA)
        self.assertIn("ota_flash_yields.fetch_add(1)",UPLOAD)
        self.assertIn("std::memcmp(digest,release.digest,32)!=0",OTA)
    def test_transport_and_flash_are_disjoint(self):
        self.assertIn("MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT", UPLOAD)
        self.assertIn("ota_staging_psram_reserve_bytes", UPLOAD)
        self.assertLess(UPLOAD.index("ota::stage_image"), UPLOAD.index("close_https(client)"))
        self.assertLess(UPLOAD.index("close_https(client)"), UPLOAD.index("if(received!=ota::StageResult::Complete)"))
        self.assertLess(UPLOAD.index("if(received!=ota::StageResult::Complete)"), UPLOAD.index("esp_ota_begin"))
        self.assertLess(UPLOAD.index("esp_ota_begin"), UPLOAD.index("esp_ota_write"))
        self.assertIn("std::memcpy(data,image.get()+done,chunk)", UPLOAD)
        self.assertIn("esp_ota_write(handle,data,chunk)", UPLOAD)
        self.assertLess(UPLOAD.index("esp_ota_end(handle)"), UPLOAD.index("esp_ota_set_boot_partition(target)"))
        self.assertIn("ota_download_deadline_ms", OTA)
        self.assertIn("esp_http_client_is_complete_data_received", OTA)
        self.assertIn('"ota_programmed_bytes"', APP)
    def test_no_auto_pump_write_and_maintenance_only_for_signed_eligible(self):
        self.assertIn("ota::signed_version_eligible",WORKER)
        self.assertLess(WORKER.index("ota::signed_version_eligible"),
                        WORKER.index("pause_readers.store(true)"))
        self.assertLess(WORKER.index("network_budget_release()"),
                        WORKER.index("pause_readers.store(true)"))
        self.assertIn("pause_readers.store(false)",WORKER)
        self.assertIn("!cottage_ota_network_maintenance()",APP)
        self.assertIn("if(cottage_ota_network_maintenance())",APP)
        self.assertNotIn("control_armed.store(",WORKER)
        self.assertIn("cottage_ota_initialize()",WORKER)
    def test_manifest_budget_and_download_budget_separate(self):
        self.assertIn("ota_manifest_min_internal_bytes",WORKER)
        self.assertIn("ota_image_min_internal_bytes",WORKER)
        self.assertIn("ota_min_contiguous_internal_bytes",WORKER)
        self.assertIn("ota_readers_release_ms",WORKER)
        self.assertIn('last_status.store(applied?6:4)',WORKER)
    def test_attempt_marker_prevents_unattended_retry_even_after_reset(self):
        self.assertIn("ota_manual_pending.exchange(false)",WORKER)
        self.assertLess(WORKER.index("read_attempted_version(attempted)"),
                        WORKER.index("ota::image_attempt_allowed"))
        self.assertLess(WORKER.index("ota::signed_version_eligible"),
                        WORKER.index("ota::image_attempt_allowed"))
        self.assertLess(WORKER.index("remember_image_attempt(r.version,explicit_request)"),
                        WORKER.index("applied=update_app(r)"))
        self.assertIn("nvs_set_u32(h,kAttemptedVersionKey,version)",OTA)
        self.assertIn("nvs_commit(h)==ESP_OK",OTA)
        self.assertIn("RetryRequiresRequest",WORKER)
    def test_telemetry_and_control_separate(self):
        self.assertIn('"ota_downloaded_bytes"',APP)
        self.assertIn('"ota_idle_yields"',APP)
        self.assertIn("cottage_control_tick(bridge);",APP)

if __name__=="__main__":unittest.main()
