"""Release gate: OTA must diagnose original memory gates and retry boundedly."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
POLICY=(ROOT/"cottage/ota_runtime_policy.hpp").read_text()
SOURCE=(ROOT/"firmware/cottage/src/cottage_secure_ota.cpp").read_text()
WIRE=(ROOT/"firmware/cottage/src/main.cpp").read_text()


class OTARecoverySource(unittest.TestCase):
    def test_original_memory_gates_not_reduced(self):
        self.assertIn("ota_manifest_min_internal_bytes=72u*1024u", POLICY)
        self.assertIn("ota_image_min_internal_bytes=76u*1024u", POLICY)
        self.assertIn("ota_min_contiguous_internal_bytes=20u*1024u", POLICY)
        self.assertIn("ota_flash_idle_delay_ticks=2u", POLICY)

    def test_image_stage_has_bounded_retries_with_unmodified_guards(self):
        self.assertIn("ota_image_lease_max_attempts=12u", POLICY)
        self.assertIn("ota_image_lease_retry_delay_ms=400u", POLICY)
        self.assertIn("attempt<=enevina::cottage::ota_image_lease_max_attempts", SOURCE)
        self.assertIn("ota_image_attempts.store(attempt);", SOURCE)
        self.assertIn("enevina::cottage::ota_image_min_internal_bytes,", SOURCE)
        self.assertIn("enevina::cottage::ota_min_contiguous_internal_bytes,", SOURCE)
        self.assertIn("&image_diag))", SOURCE)
        self.assertIn("if(!image_lease)", SOURCE)
        self.assertIn("if(attempt<enevina::cottage::ota_image_lease_max_attempts)", SOURCE)
        self.assertNotIn("setInsecure(",SOURCE)

    def test_stage_diagnostic_not_inferred_from_late_heap_sampling(self):
        for name in ("ota_problem_stage","ota_problem_reason","ota_problem_free_internal",
                     "ota_problem_largest_internal","ota_image_lease_attempts"):
            self.assertIn('"' + name + '"',WIRE)
        for stage in ("ManifestLease","Storage","ImageLease","Transport"):
            self.assertIn("OtaProblemStage::"+stage,SOURCE)
        for cause in ("MutexBusy","LowMemory","NvsUnavailable","WifiLost","ImageFailed"):
            self.assertIn("OtaProblemReason::"+cause,SOURCE)

    def test_ota_keeps_signed_validation_and_recovery(self):
        for marker in ("verify_manifest(","ota::signed_version_eligible(",
                       "esp_ota_set_boot_partition(","esp_partition_get_sha256(",
                       "ota::Transaction tx", "ota_readers_release_ms",
                       "pause_readers.store(false)"):
            self.assertIn(marker,SOURCE)
        self.assertIn("last_status.store(applied?6:4)",SOURCE)


if __name__=="__main__":
    unittest.main()
