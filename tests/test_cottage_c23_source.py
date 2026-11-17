"""C2.3 source-level invariants. Not a substitute for physical BLE/NVS tests."""
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]
SERVICE=(ROOT/"firmware/cottage/src/cottage_air_service.cpp").read_text(encoding="utf8")
APP=(ROOT/"firmware/cottage/src/main.cpp").read_text(encoding="utf8")
CORE=(ROOT/"cottage/air_humidity.cpp").read_text(encoding="utf8")

class C23SourceTests(unittest.TestCase):
    def test_no_physical_actuation(self):
        self.assertIn("physical_writes_allowed",SERVICE)
        self.assertIn('cJSON_AddBoolToObject(air,"physical_writes_allowed",false)',SERVICE)
        for prohibited in (
            "Switch.Set","Switch.Toggle","setClimate","setSetpoint",
            "Mitsubishi.Set","sendIr(","setHeatPump","Power.Off",
            "climate.setTemperature","HTTPClient", "WiFiClient"):
            self.assertNotIn(prohibited,SERVICE)
        self.assertNotIn("COTTAGE_SYNTHETIC_TEST_MODEL 1",SERVICE)
        self.assertIn("c23_ble_worker",APP)
        self.assertNotIn('if(strlen(COTTAGE_RUUVI_MAC))',APP)
        self.assertNotIn("xTaskCreatePinnedToCore(ruuvi_worker",APP)

    def test_sensor_identity_and_dedup(self):
        self.assertIn("RuuviBinding",SERVICE)
        self.assertIn("decode_ruuvi_v5(",SERVICE)
        self.assertIn("RuuviAccept::Duplicate",SERVICE)
        self.assertIn("indoor_binding.bound()",SERVICE)
        self.assertIn('p.putString("indoor",indoor_id)',SERVICE)
        self.assertIn('prefs.getString(key,"")',SERVICE)

    def test_offline_ble_and_persistence(self):
        body=SERVICE.split("void c23_ble_worker(void*)",1)[1].split("void c23_tick()",1)[0]
        self.assertNotIn("WiFi.status()",body)
        self.assertIn("scan->start(4,false)",body)
        self.assertIn("COTTAGE_RUUVI_MAC",SERVICE)
        self.assertIn('p.putUChar("mode"',SERVICE)
        self.assertIn('prefs.getUChar("mode"',SERVICE)
        self.assertIn('hp.getBytes("hourly_v1"',SERVICE)
        self.assertIn('prefs.putBytes("hourly_v1"',SERVICE)
        self.assertIn("6ULL*3600*1000",SERVICE)

    def test_sensor_rebinding_does_not_mix_history(self):
        self.assertIn("history=AirHistory{}",SERVICE)
        self.assertIn('p.remove("hourly_v1")',SERVICE)
        self.assertIn("binding_changed",SERVICE)

    def test_ui_settings_only(self):
        self.assertIn('web.on("/api/air/config",HTTP_POST',SERVICE)
        self.assertIn('web.on("/api/air/thresholds",HTTP_POST',SERVICE)
        self.assertIn('web.on("/api/air/bind",HTTP_POST',SERVICE)
        self.assertIn('web.on("/api/air/ack",HTTP_POST',SERVICE)
        self.assertIn("check_token(web)",SERVICE)
        self.assertIn("air-panel", APP)
        self.assertIn("ERILLINEN OHJAUSLUPA",APP)
        self.assertIn("tila tai tavoite ei yksin lähetä Mitsubishi-komentoja",APP)

    def test_alerts_and_no_wall_certification(self):
        self.assertIn("24ULL*3600000", (ROOT/"cottage/air_humidity.hpp").read_text())
        self.assertIn("600000", (ROOT/"cottage/air_humidity.hpp").read_text())
        self.assertIn("air_engine",SERVICE.lower().replace("alarm_engine","air_engine"))
        self.assertIn("possible_drying",SERVICE)
        self.assertIn("estimate_ventilation",SERVICE)
        self.assertIn("Rakenteen kosteus- tai",APP)


if __name__ == "__main__":
    unittest.main()
