"""ESP32-S3 memory admission regression: never weaken BLE, price or OTA gates."""
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()
MIDEA=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
WEATHER=(ROOT/"firmware/main/weather.cpp").read_text()
MARKET=(ROOT/"firmware/main/market.cpp").read_text()
OTA=(ROOT/"cottage/ota_runtime_policy.hpp").read_text()
MEMORY=(ROOT/"cottage/memory_policy.hpp").read_text()

class TestInternalRam(unittest.TestCase):
    def test_pinned_nimble_preserves_ruuvi_scan_and_margin(self):
        config=(ROOT/"firmware/cottage/platformio.ini").read_text()
        air=(ROOT/"firmware/cottage/src/cottage_air_service.cpp").read_text()
        self.assertIn("h2zero/NimBLE-Arduino@1.4.3",config)
        self.assertIn("class OnAdvertisement : public NimBLEAdvertisedDeviceCallbacks",air)
        self.assertIn("void onResult(NimBLEAdvertisedDevice* device)",air)
        self.assertIn("decode_ruuvi_v5(",air)
        self.assertIn("sensor_registry.observe(parsed,ms)",air)
        self.assertIn("NimBLEDevice::getScan()",air)
        self.assertIn("if(!scan)",air)
        self.assertIn('c23_ble_worker,"cottage_ruuvi",6144',MAIN)
        self.assertIn('xTaskCreate(poll_prices,"market_read",8192',MARKET)
    def test_no_phantom_midea_stack_when_other_pump_is_selected(self):
        self.assertIn("cottage_midea_service_needs_worker()",MAIN)
        self.assertIn("!is_midea_selected() || !mutex",MIDEA)
        self.assertIn("const bool ready=configured;",MIDEA)
        self.assertIn("if(!task_midea &&",MAIN)
        self.assertIn("&task_midea,0)",MAIN)
        self.assertNotIn('nullptr,1,nullptr,0)',MAIN.split("cottage_midea_service_worker",1)[1].split("verify_new_ota_boot",1)[0])

    def test_weather_worker_created_after_site_selection(self):
        self.assertIn("if(configured)ensure_weather_reader();",WEATHER)
        self.assertIn("if(enabled && !ensure_weather_reader())return false;",WEATHER)
        self.assertIn('xTaskCreate(poll_weather,"weather_read",7168',WEATHER)
        self.assertIn("if(changed && weather_task)xTaskNotifyGive(weather_task)",WEATHER)

    def test_existing_tls_and_ble_memory_guards_not_reduced(self):
        for v in ("72u*1024u","76u*1024u","20u*1024u","24u*1024u"):
            self.assertIn(v,OTA)
        self.assertIn("64u*1024u",MEMORY)
        self.assertIn("NetworkBudgetLease network_lease(8000, 78*1024, 16*1024)",MARKET)
        self.assertIn("if(!enevina_firmware::network_budget_acquire(200,", (ROOT/"firmware/cottage/src/cottage_secure_ota.cpp").read_text())

    def test_observable_headroom_including_ota_and_history(self):
        for name in ("ota_stack_free","history_stack_free","midea_stack_free",
                     "weather_stack_free","market_stack_free"):
            self.assertIn('"'+name+'"',MAIN)

if __name__=="__main__":
    unittest.main()
