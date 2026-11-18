"""Source regressions for the owner's 30 min HEAT/OFF dwell wiring (18.11.2026).

The policy itself is host-tested in tests/cottage_mitsubishi_control_tests.cpp;
these checks pin how firmware feeds it. Not a physical pump test.
"""
import unittest
from pathlib import Path

MAIN = (Path(__file__).resolve().parents[1] / "firmware" / "cottage" / "src" /
        "main.cpp").read_text(encoding="utf-8")
TICK = MAIN.split("static void cottage_control_tick() {", 1)[1].split(
    "static void cottage_control_registration()", 1)[0]


class ModeDwellWiring(unittest.TestCase):
    def test_snapshot_gets_time_since_last_mode_change(self):
        self.assertIn("candidate.ms_since_mode_change=", TICK)
        self.assertLess(TICK.index("control_note_observed_mode(current_heat);"),
                        TICK.index("choose_mitsubishi_control(candidate)"))

    def test_mode_command_starts_dwell_before_dispatch(self):
        start = TICK.index("control_mode_change_ms.store(millis());")
        self.assertLess(start, TICK.index("cn105_bridge_post(request"))

    def test_readback_and_arming_update_history(self):
        self.assertIn("control_note_observed_mode(read_heating);", TICK)
        arm = MAIN.split('web.on("/api/control/arm"', 1)[1]
        self.assertLess(arm.index("control_mode_change_ms.store(millis());"),
                        arm.index("control_armed.store(requested)"))

    def test_boot_starts_with_unknown_history(self):
        self.assertIn("static std::atomic<uint32_t> control_mode_change_ms{0};", MAIN)


if __name__ == "__main__":
    unittest.main()
