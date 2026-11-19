from pathlib import Path
import unittest
P=(Path(__file__).resolve().parents[1]/
   "firmware/cottage/src/cottage_secure_ota.cpp").read_text(encoding="utf-8")
class OTAImageTimeout(unittest.TestCase):
 def test_bounded_image_timeout_and_short_manifest(self):
    section=P.split("esp_http_client_handle_t open_https(",1)[1].split("void close_https(",1)[0]
    self.assertIn("cfg.timeout_ms=expected_length>512 ? 20000 : 8500;",section)
    self.assertIn("cfg.cert_pem=COTTAGE_OTA_LOCAL_ROOT_CA;",section)
    self.assertIn("cfg.disable_auto_redirect=true;",section)
 def test_no_memory_floor_or_retry_safety_downgrade(self):
    self.assertIn("heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<24*1024",P)
    self.assertIn("if(WiFi.status()!=WL_CONNECTED",P)
    self.assertIn("std::memcmp(digest,release.digest,32)!=0",P)
    self.assertIn("esp_ota_abort(handle);",P)
if __name__=="__main__":unittest.main()
