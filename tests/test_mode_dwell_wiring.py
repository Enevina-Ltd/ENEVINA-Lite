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
POST = MAIN.split("static bool cn105_bridge_post(", 1)[1].split(
    "static void cottage_control_tick()", 1)[0]


class ModeDwellWiring(unittest.TestCase):
    def test_snapshot_gets_time_since_last_mode_change(self):
        self.assertIn("candidate.ms_since_mode_change=", TICK)
        self.assertLess(TICK.index("control_note_observed_mode(current_heat);"),
                        TICK.index("choose_mitsubishi_control(candidate)"))

    def test_dwell_starts_at_actual_dispatch(self):
        # Codex P2 (post-merge #28): only after all preconditions, right
        # before the POST; never in the tick before preparation can fail.
        self.assertNotIn("control_note_mode_dispatch()", TICK)
        start = POST.index("control_note_mode_dispatch();")
        self.assertLess(start, POST.index('req.POST("")'))
        self.assertGreater(start, POST.rindex("c23_config_generation()!=config_generation", 0,
                                              POST.index('req.POST("")')))

    def test_readback_and_arming_update_history(self):
        self.assertIn("control_note_observed_mode(read_heating);", TICK)
        arm = MAIN.split('web.on("/api/control/arm"', 1)[1].split('control_save_user_arm(record)',1)[1]
        self.assertLess(arm.index("control_reset_mode_history();"),
                        arm.index("control_armed.store(requested)"))

    def test_history_is_shared_class_with_lock(self):
        self.assertIn("static MitsubishiModeHistory control_mode_history{};", MAIN)
        self.assertIn("candidate.ms_since_mode_change=control_ms_since_mode_change();", TICK)
        self.assertIn("portENTER_CRITICAL(&mode_history_lock);", MAIN)


if __name__ == "__main__":
    unittest.main()
