"""Keep runtime measurement and bounded optional discovery lifetime."""
from pathlib import Path
import unittest
R=Path(__file__).resolve().parents[1]/"firmware/cottage/src"
MAIN=(R/"main.cpp").read_text()
SHELLY=(R/"cottage_shelly_discovery.cpp").read_text()
BLE=(R/"cottage_air_service.cpp").read_text()
BUDGET=(R/"cottage_network_budget.cpp").read_text()
class EvidenceTests(unittest.TestCase):
    def test_sdk_min_and_live_samples_are_both_reported(self):
        self.assertIn('heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)',MAIN)
        self.assertIn('heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)',MAIN)
        self.assertIn('runtime_sampled_min_internal',MAIN)
        self.assertIn('runtime_heap_samples',MAIN)
        self.assertIn('uint32_t(now-last)<250u',MAIN)
        self.assertIn('heap_caps_register_failed_alloc_callback(heap_allocation_failed)',MAIN)
        callback=MAIN.split('static void heap_allocation_failed(',1)[1].split('static void sample_runtime_heap()',1)[0]
        self.assertNotIn('Serial.',callback)
        self.assertNotIn('malloc(',callback)
    def test_discovery_worker_releases_internal_stack(self):
        body=SHELLY.split('void worker_main(void*)',1)[1].split('} // namespace',1)[0]
        self.assertIn('run_scan();',body)
        self.assertIn('vTaskDelete(nullptr);',body)
        self.assertNotIn('ulTaskNotifyTake',body)
        self.assertIn('xTaskCreate(worker_main,"shelly_scan",7168,nullptr,2,nullptr)',SHELLY)
        self.assertIn('const bool pending=shared.state==ShellyScanState::Pending',SHELLY)
    def test_diagnostics_keep_denials_and_distinguish_causes(self):
        self.assertIn('busy_refusals.fetch_add(1)',BUDGET)
        self.assertIn('memory_refusals.fetch_add(1)',BUDGET)
        for key in ('web_idle_timeouts','web_partial_timeouts','web_idle_closes','web_idle_evictions','web_partial_closes','web_invalid_requests','web_resource_rejects','web_completed_requests','network_budget_low_memory_denials','heap_allocation_failures'):
            self.assertIn('"'+key+'"',MAIN)
    def test_ruuvi_callback_path_is_unchanged(self):
        self.assertIn('scan->setMaxResults(0)',BLE)
        self.assertIn('ble_advertisements.fetch_add(1)',BLE)
        self.assertIn('ble_ruuvi_advertisements.fetch_add(1)',BLE)
        self.assertIn('sensor_registry.observe(parsed,ms)',BLE)
if __name__=="__main__":unittest.main()
