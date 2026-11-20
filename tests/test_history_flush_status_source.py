"""Regression: a mounted LittleFS partition is not proof the latest flush succeeded."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = (ROOT / "firmware/cottage/src/cottage_history.cpp").read_text(encoding="utf-8")


class HistoryFlushStatusTests(unittest.TestCase):
    def test_api_persistence_flag_reflects_latest_successful_flush(self):
        self.assertIn("bool persistence_healthy=false;", FIRMWARE)
        self.assertIn("const bool saved=persistent&&persistence&&persistence_healthy;", FIRMWARE)
        self.assertIn("persistence_healthy=ok;", FIRMWARE)

    def test_failed_flush_does_not_advance_success_bookkeeping(self):
        self.assertIn("const bool ok=persistence->flush(*store,events);", FIRMWARE)
        body = FIRMWARE.split("const bool ok=persistence->flush(*store,events);", 1)[1]
        self.assertLess(body.index("if(ok) {"), body.index("last_flush_ms=now;"))
        self.assertLess(body.index("if(ok) {"), body.index("events_total_at_flush=events.total();"))
        self.assertIn("last_attempt_ms=now;", FIRMWARE)
        self.assertIn("now-last_attempt_ms>=kRetryIntervalMs", FIRMWARE)
        self.assertIn("persistence_healthy||now-last_attempt_ms>=kRetryIntervalMs", FIRMWARE)


if __name__ == "__main__":
    unittest.main()
