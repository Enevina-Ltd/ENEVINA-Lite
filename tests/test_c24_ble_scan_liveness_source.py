from pathlib import Path
import unittest
R=Path(__file__).resolve().parents[1]
SRC=(R/"firmware/cottage/src/cottage_air_service.cpp").read_text(encoding="utf-8")
POLICY=(R/"cottage/memory_policy.hpp").read_text(encoding="utf-8")
class ScanLiveness(unittest.TestCase):
 def test_root_cause_corrected_not_tls_limits(self):
    self.assertIn("ble_scan_min_internal_bytes=64u*1024u",POLICY)
    self.assertIn("ble_scan_min_contiguous_bytes=20u*1024u",POLICY)
    self.assertNotIn("ble_scan_min_internal_bytes=80u*1024u",POLICY)
    ota=(R/"cottage/ota_runtime_policy.hpp").read_text(encoding="utf-8")
    self.assertIn("ota_image_min_internal_bytes=76u*1024u",ota)
 def test_ble_telemetry_and_exclusive_network_budget(self):
    loop=SRC.split("void c23_ble_worker(void*) {",1)[1].split("bool c23_ble_initialized()",1)[0]
    self.assertIn("NetworkBudgetLease lease(0,",loop)
    self.assertIn("ble_scan_attempts.fetch_add(1)",loop)
    self.assertIn("ble_scan_completed.fetch_add(1)",loop)
    self.assertIn("ble_scan_memory_denials.fetch_add(1)",loop)
    self.assertIn("ble_scan_busy_denials.fetch_add(1)",loop)
    self.assertIn("scan->start(4,false)",loop)
    self.assertIn("scan->clearResults()",loop)
    self.assertIn("vTaskDelay(pdMS_TO_TICKS(12000))",loop)
    self.assertNotIn("control_armed.store(",loop)
    self.assertNotIn("nvs_flash_erase",loop)
 def test_status_exposes_liveness(self):
    for name in ("ble_scan_attempts","ble_scan_completed","ble_scan_memory_denials",
                 "ble_scan_busy_denials","ble_scan_last_free_internal",
                 "ble_scan_last_largest_internal"):
        self.assertIn('cJSON_AddNumberToObject(air,"'+name+'"',SRC)
if __name__=="__main__": unittest.main()
