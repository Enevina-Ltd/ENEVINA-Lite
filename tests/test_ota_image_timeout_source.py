from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
P=(ROOT/"firmware/cottage/src/cottage_secure_ota.cpp").read_text(encoding="utf-8")
POLICY=(ROOT/"cottage/ota_runtime_policy.hpp").read_text(encoding="utf-8")
class OTAImageTimeout(unittest.TestCase):
 def test_bounded_image_timeout_and_short_manifest(self):
    section=P.split("esp_http_client_handle_t open_https(",1)[1].split("void close_https(",1)[0]
    self.assertIn("cfg.timeout_ms=expected_length>512 ? 20000 : 8500;",section)
    self.assertIn("cfg.cert_pem=COTTAGE_OTA_LOCAL_ROOT_CA;",section)
    self.assertIn("cfg.disable_auto_redirect=true;",section)
 def test_no_memory_floor_or_retry_safety_downgrade(self):
    # Same 24 KiB threshold, now a shared constant with bounded rechecks.
    self.assertIn("ota_inflight_min_internal_bytes=24u*1024u;",POLICY)
    self.assertIn("if(internal_free<enevina::cottage::ota_inflight_min_internal_bytes)",P)
    self.assertIn("ota_inflight_recheck_max_waits=8u",POLICY)
    self.assertIn("ota_inflight_recheck_delay_ms=40u",POLICY)
    self.assertIn("OtaProblemReason::InflightLowMemory",P)
    self.assertIn("if(WiFi.status()!=WL_CONNECTED",P)
    self.assertIn("std::memcmp(digest,release.digest,32)!=0",P)
    self.assertIn("esp_ota_abort(handle);",P)
if __name__=="__main__":unittest.main()
