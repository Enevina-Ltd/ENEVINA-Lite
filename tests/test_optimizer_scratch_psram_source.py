"""Real N16R8 CN105/Shelly memory-recovery contract for the MPC scratch."""
import unittest
from pathlib import Path

MAIN=(Path(__file__).resolve().parents[1]/
      "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
WORKER=MAIN.split("static void optimizer_worker(void*) {",1)[1].split(
    "static void history_worker(void*)",1)[0] if "static void history_worker(void*)" in MAIN.split("static void optimizer_worker(void*) {",1)[1] else MAIN.split("static void optimizer_worker(void*) {",1)[1]

class OptimizerPsram(unittest.TestCase):
    def test_large_scratch_allocated_in_psram_not_internal_heap(self):
        self.assertIn("constexpr auto scratch_caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;",WORKER)
        for ty,cap in (
            ("enevina_firmware::PricePoint","200"),
            ("enevina::OutdoorPoint","64"),
            ("Spot15","200"),
        ):
            self.assertIn(f"sizeof({ty})*{cap},scratch_caps)",WORKER)
        for previous in ("new enevina_firmware::PricePoint[200]",
                         "new enevina::OutdoorPoint[64]",
                         "new Spot15[200]",
                         "new (std::nothrow) Row[max_horizon_slots]"):
            self.assertNotIn(previous,WORKER)

    def test_construct_and_fail_closed_if_psram_unavailable(self):
        for expression in (
            "if(prices) for(size_t i=0;i<200;++i) new(prices+i)",
            "if(weather) for(size_t i=0;i<64;++i) new(weather+i)",
            "if(aligned) for(size_t i=0;i<200;++i) new(aligned+i)",
            "if(wifi_ok.load() && clock_ok() && prices && weather && aligned && hz && rows &&",
            "trend && id",
        ):
            self.assertIn(expression,WORKER)
        self.assertNotIn("plan_rows",MAIN)
        self.assertNotIn("sizeof(Row)*max_horizon_slots",WORKER)
        self.assertIn("PSRAM scratch unavailable; planning disabled",WORKER)
        self.assertIn("MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT",WORKER)
        self.assertIn("cottage_ota_network_maintenance()",MAIN)
        self.assertIn("control_armed.load()",MAIN)

if __name__=="__main__":unittest.main()
