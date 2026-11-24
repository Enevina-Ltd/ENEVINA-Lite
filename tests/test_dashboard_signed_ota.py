"""Dashboard can only ask the existing signed OTA worker to check/apply."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "firmware/cottage/src/main.cpp").read_text()
OTA = (ROOT / "firmware/cottage/src/cottage_secure_ota.cpp").read_text()
UI = (ROOT / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text()
HEADER = (ROOT / "firmware/cottage/include/cottage_secure_ota.hpp").read_text()
START = "const UI_EN=Object.freeze("
END = ");\nconst UI_EN_FRAGMENTS"
LANG = json.loads(UI.split(START, 1)[1].split(END, 1)[0])


class DashboardSignedOTA(unittest.TestCase):
    def test_ota_endpoint_requires_existing_token_and_trusted_wifi(self):
        api = APP.split('web.on("/api/ota/install",HTTP_POST,[](){', 1)[1].split(
            'web.on("/api/plan"', 1)[0]
        self.assertIn("c23_authorize_local_setting", api)
        self.assertIn("cottage_wifi_request_via_ap", api)
        self.assertIn("WiFi.status()!=WL_CONNECTED", api)
        self.assertIn("cottage_ota_configured()", api)
        self.assertIn("cottage_ota_confirmation_pending()", api)
        self.assertIn("cottage_ota_request_immediate()", api)
        self.assertIn("web.send(202", api)
        self.assertNotIn("esp_ota_begin(", api)
        self.assertNotIn("Update.write(", api)
        self.assertNotIn("HTTPUpload", api)

    def test_existing_signed_verification_and_rollback_remain_authoritative(self):
        self.assertIn("bool cottage_ota_request_immediate();", HEADER)
        self.assertIn("ota_worker_task.store(xTaskGetCurrentTaskHandle())", OTA)
        self.assertIn("xTaskNotifyGive(task)", OTA)
        self.assertIn("ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(60000))", OTA)
        # A request queued during the first 90 seconds is serviced by the
        # initial check. Drain that ONE old notification before checking so
        # it cannot trigger an immediate retry after the first download fails.
        worker_start = OTA.split("void cottage_ota_worker(void*) {", 1)[1]
        self.assertLess(
            worker_start.index("vTaskDelay(pdMS_TO_TICKS(90000))"),
            worker_start.index("ulTaskNotifyTake(pdTRUE,0)"),
        )
        self.assertLess(
            worker_start.index("ulTaskNotifyTake(pdTRUE,0)"),
            worker_start.index("while(true)"),
        )
        # Initial boot notification is drained once before the loop. A
        # second drain is only permitted after an actual image attempt,
        # while maintenance still rejects new dashboard requests.
        self.assertEqual(worker_start.split("while(true)",1)[0].count("ulTaskNotifyTake(pdTRUE,0)"),1)
        self.assertEqual(worker_start.count("ulTaskNotifyTake(pdTRUE,0)"),2)
        post_attempt=worker_start.split("last_status.store(applied?6:4)",1)[1]
        self.assertLess(post_attempt.index("ota_manual_pending.exchange(false)"),
                        post_attempt.index("ulTaskNotifyTake(pdTRUE,0)"))
        self.assertLess(post_attempt.index("ulTaskNotifyTake(pdTRUE,0)"),
                        post_attempt.index("pause_readers.store(false)"))
        # Manual button must also wake the ten-minute post-failure wait.
        self.assertIn("ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10*60*1000))", OTA)
        self.assertNotIn("vTaskDelay(pdMS_TO_TICKS(10*60*1000))", OTA)
        worker = OTA.split("void cottage_ota_worker(void*) {", 1)[1]
        self.assertIn("fetch_manifest(r)", worker)
        self.assertIn("ota::signed_version_eligible", worker)
        self.assertIn("update_app(r)", worker)
        self.assertLess(worker.index("fetch_manifest(r)"), worker.index("update_app(r)"))
        self.assertIn("esp_ota_begin(target,OTA_WITH_SEQUENTIAL_WRITES,&handle)", OTA)
        self.assertIn("mbedtls_pk_verify", OTA)
        self.assertIn("esp_ota_set_boot_partition", OTA)
        self.assertNotIn('web.on("/api/ota/upload"', APP)

    def test_dashboard_action_explicit_confirmation_progress_and_translations(self):
        for key in (
            "Firmwarepäivitys", "Tarkista ja asenna päivitys",
            "Päivityksen tila", "Viimeisin tarkistus",
            "Tarkistetaanko allekirjoitettu firmware ja asennetaanko uusi versio, jos sellainen löytyy? Laite voi käynnistyä uudelleen.",
            "Allekirjoitettua OTA-päivityskanavaa ei ole tällä laitteella. Päivitystä ei voi käynnistää ennen sen käyttöönottoa.",
        ):
            self.assertIn(key, LANG)
            self.assertNotEqual(LANG[key], key)
        self.assertIn("window.confirm(translateFi(", UI)
        self.assertIn("'/api/ota/install'", UI)
        self.assertIn("ota_signed_pull_enabled===true", UI)
        self.assertIn("ota_dashboard_pending", UI)
        self.assertIn("ota_downloaded_bytes", UI)
        self.assertIn('id="firmware-update-card"', UI)
        self.assertIn("Tavallisen .bin-tiedoston lataus selaimesta ei ole käytössä", UI)


if __name__ == "__main__":
    unittest.main()