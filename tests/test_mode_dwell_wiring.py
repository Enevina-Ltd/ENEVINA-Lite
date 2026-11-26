"""Source regressions for the owner's 30 min HEAT/OFF dwell wiring (18.11.2026).

The policy itself is host-tested in tests/cottage_mitsubishi_control_tests.cpp;
these checks pin how firmware feeds it. Not a physical pump test.
"""
import unittest
from pathlib import Path

MAIN = (Path(__file__).resolve().parents[1] / "firmware" / "cottage" / "src" /
        "main.cpp").read_text(encoding="utf-8")
TICK = MAIN.split("static void cottage_control_tick(const enevina::PumpRoomState& bridge) {", 1)[1].split(
    "static void cottage_control_registration()", 1)[0]
POST = MAIN.split("static bool cn105_bridge_post(", 1)[1].split(
    "static void cottage_control_tick(const enevina::PumpRoomState& bridge)", 1)[0]


class ModeDwellWiring(unittest.TestCase):
    def test_snapshot_gets_time_since_last_mode_change(self):
        self.assertIn("candidate.ms_since_mode_change=", TICK)
        worker=MAIN.split("static void optimizer_worker(void*) {",1)[1]
        self.assertLess(worker.index("cn105_room_state()"),worker.index("cottage_control_tick(bridge);"))
        self.assertIn("choose_mitsubishi_control(candidate)",TICK)

    def test_early_stop_is_scoped_and_revalidated(self):
        preview=MAIN.split("static bool cn105_low_temperature_cycle_selected(",1)[1].split("static void mpc_mode_target",1)[0]
        self.assertIn("return mode==CottageMode::Hold",preview)
        self.assertIn("input.low_temperature_cycle=cn105_low_temperature_cycle_selected",preview)
        self.assertIn("enevina::adapter_allowed_cn105_physical_writes(site.pump_adapter)",preview)
        self.assertIn("site_room_temperature_config().source==RoomTemperatureSource::Ruuvi",preview)
        self.assertIn("low_temperature_room_target(target_c)",preview)
        self.assertIn("pi.low_temperature_cycle=heating_allowed",MAIN)
        self.assertIn("meta.low_temperature_cycle=has_target",MAIN)
        self.assertIn("mitsubishi_min_mode_dwell_ms,latest,true",POST)
        self.assertGreater(POST.index("mitsubishi_min_mode_dwell_ms,latest,true"),POST.index("const auto latest=active_control_proposal"))
        self.assertIn("mitsubishi_min_mode_dwell_ms,refreshed,true",POST)
        self.assertIn("mode_change_allowed(step,control_ms_since_mode_change(),mitsubishi_min_mode_dwell_ms,refreshed,true)",POST)
        self.assertIn("const bool dispatch=!defrost_hold",POST)
        self.assertIn("cn105_reconciliation.unresolved",POST)

    def test_dwell_starts_at_actual_dispatch(self):
        # Codex P2 (post-merge #28): only after all preconditions, right
        # before the POST; never in the tick before preparation can fail.
        self.assertNotIn("control_note_mode_dispatch()", TICK)
        start = POST.index("control_note_mode_dispatch();")
        self.assertLess(start, POST.index('req.POST("")'))
        self.assertGreater(start, POST.rindex("c23_config_generation()!=config_generation", 0,
                                              POST.index('req.POST("")')))

    def test_history_is_observed_even_when_disarmed_without_resetting_on_arm(self):
        worker=MAIN.split("static void optimizer_worker(void*) {",1)[1]
        self.assertLess(worker.index("cn105_room_state()"),worker.index("cottage_control_tick(bridge);"))
        self.assertNotIn("control_armed.load()",worker[:worker.index("cn105_room_state()")])
        arm = MAIN.split('web.on("/api/control/arm"', 1)[1].split(
            "static uint64_t cottage_now_ms",1)[0]
        self.assertNotIn("control_reset_mode_history", arm)
        self.assertIn("control_note_observed_mode(read_mode,", TICK)

    def test_post_requires_current_authorization_and_no_ota_maintenance(self):
        last_check = POST.split("const bool dispatch=", 1)[1].split("if(dispatch)", 1)[0]
        self.assertIn("control_permission_generation.load()==authorization_generation", last_check)
        self.assertIn("!cottage_ota_network_maintenance()", last_check)
        self.assertIn("wifi_ok.load() && clock_ok()", last_check)
        self.assertNotIn("control_commands_sent.fetch_add(1)", TICK)
        self.assertLess(POST.index("control_commands_sent.fetch_add(1)"),
                        POST.index('req.POST("")'))

    def test_history_is_owned_by_the_optimizer_after_setup(self):
        self.assertIn("static MitsubishiModeHistory control_mode_history{};", MAIN)
        self.assertIn("candidate.ms_since_mode_change=control_ms_since_mode_change();", TICK)
        self.assertLess(MAIN.index("cottage_control_registration();"),
                        MAIN.index("xTaskCreatePinnedToCore(optimizer_worker"))


if __name__ == "__main__":
    unittest.main()
