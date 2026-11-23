"""N16R8 production boundaries for signed OTA, RAM headroom and errors."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
OTA=(ROOT/"firmware/cottage/src/cottage_secure_ota.cpp").read_text()
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()
MET=(ROOT/"firmware/main/weather.cpp").read_text()
SERVER=(ROOT/"tools/cottage_ota_server.py").read_text()

class TestOTAMemoryFixes(unittest.TestCase):
    def test_original_guard_remains_fail_closed(self):
        self.assertIn("ota_inflight_min_internal_bytes",OTA)
        self.assertIn("if(internal_free<enevina::cottage::ota_inflight_min_internal_bytes)",OTA)
        self.assertIn("OtaProblemReason::InflightLowMemory",OTA)
        self.assertIn("ota_problem_free.store(diag.free_internal)",OTA)
        self.assertIn("esp_ota_abort(handle)",OTA)
        self.assertIn("esp_ota_set_boot_partition(target)",OTA)

    def test_bounded_recovery_never_bypasses_inflight_guard(self):
        policy=(ROOT/"cottage/ota_runtime_policy.hpp").read_text()
        self.assertIn("ota_inflight_recheck_max_waits=8u",policy)
        self.assertIn("ota_inflight_recheck_delay_ms=40u",policy)
        self.assertIn("ota_inflight_recheck_max_waits*ota_inflight_recheck_delay_ms<=500u",policy)
        self.assertIn("ota_inflight_memory_waits.fetch_add(1)",OTA)
        self.assertIn("wait<enevina::cottage::ota_inflight_recheck_max_waits",OTA)
        self.assertIn("if(internal_free<enevina::cottage::ota_inflight_min_internal_bytes)",OTA)
        self.assertIn('ota_inflight_memory_waits",',MAIN)

    def test_image_lifecycle_errors_not_masked(self):
        for key in ("HttpOpen","FlashBegin","WifiDuringImage","InflightLowMemory",
                    "HttpReadError","HttpReadTimeout","ShaFailed",
                    "FlashWriteFailed","WrongLength","DigestMismatch",
                    "FlashEndFailed","ImageShaFailed","NvsWriteFailed",
                    "BootSelectFailed"):
            self.assertIn("OtaProblemReason::"+key,OTA)
        self.assertIn("if(!applied && ota_problem_stage.load()==0)",OTA)

    def test_sized_stacks_keep_stated_pilot_headroom(self):
        self.assertIn('xTaskCreatePinnedToCore(c23_ble_worker,"cottage_ruuvi",5120',MAIN)
        self.assertIn('xTaskCreatePinnedToCore(cottage_midea_service_worker,"cottage_midea",\n                                   8192',MAIN)
        self.assertIn('constexpr uint32_t kWeatherReaderStack=6144;',MET)
        self.assertIn('constexpr uint32_t kWeatherReaderStack=7168;',MET)
        self.assertIn("#ifdef COTTAGE_SHARED_MEMORY_GUARD",MET)
        self.assertIn('task_ota!=nullptr',MAIN)

    def test_server_keeps_tls_and_bounds_slow_stream(self):
        self.assertIn("tls.settimeout(5.0)\n                tls.do_handshake()",SERVER)
        self.assertIn("tls.do_handshake()\n                tls.settimeout(45.0)",SERVER)
        self.assertIn("request.settimeout(5.0)",SERVER)
        self.assertIn("self.allowed_clients",SERVER)
        self.assertIn("self.send_error(405)",SERVER)

if __name__=="__main__":
    unittest.main()
