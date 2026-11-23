"""P0: unused CN105/Shelly readers must not consume Midea-only internal RAM."""
from pathlib import Path
import unittest

SRC = (Path(__file__).resolve().parents[1] / "firmware/cottage/src/main.cpp").read_text()


class OptionalReaderTests(unittest.TestCase):
    def test_cn105_reader_starts_only_when_selected_and_configured(self):
        self.assertIn('std::strcmp(site.pump_adapter,"mitsubishi_cn105_esphome")==0', SRC)
        self.assertIn("valid_cn105_events_url(site.cn105_url)", SRC)
        self.assertIn("if(!task_sse && cn105_reader_needed(selected_site) &&", SRC)
        self.assertIn("xTaskCreatePinnedToCore(sse_worker", SRC)
        self.assertEqual(SRC.count("xTaskCreatePinnedToCore(sse_worker"), 1)

    def test_meter_reader_is_optional_but_starts_when_configured(self):
        self.assertIn("valid_shelly_base_url(site.shelly_url)", SRC)
        self.assertIn("if(!task_shelly && shelly_reader_needed(selected_site) &&", SRC)
        self.assertEqual(SRC.count("xTaskCreatePinnedToCore(shelly_worker"), 1)

    def test_boot_health_requires_only_needed_workers(self):
        self.assertIn("const bool readers_ready=", SRC)
        self.assertIn("(!cn105_reader_needed(selected_site) || task_sse!=nullptr)", SRC)
        self.assertIn("(!shelly_reader_needed(selected_site) || task_shelly!=nullptr)", SRC)
        self.assertIn("readers_ready && task_optimizer!=nullptr && task_ota!=nullptr", SRC)
        self.assertNotIn("task_sse!=nullptr && task_shelly!=nullptr", SRC)


if __name__ == "__main__":
    unittest.main()
