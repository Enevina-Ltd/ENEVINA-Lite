"""Source guards for the calibrated MPC (PR #63) combined with the settings /
profile / timeline / history / OTA stack (#57-#62). NOT a hardware proof."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
AIR = (ROOT / "firmware/cottage/src/cottage_air_service.cpp").read_text(encoding="utf-8")
HIST = (ROOT / "cottage/history_store.hpp").read_text(encoding="utf-8")
SETTINGS = (ROOT / "cottage/site_settings.hpp").read_text(encoding="utf-8")
TICK = MAIN.split("static void cottage_control_tick() {", 1)[1].split(
    "static void cottage_control_registration()", 1)[0]
WORKER = MAIN.split("static void optimizer_worker(void*) {", 1)[1].split(
    "static void status_endpoint()", 1)[0]


class ShellyScope(unittest.TestCase):
    def test_only_the_owners_heat_pump_only_confirmation_feeds_the_model(self):
        fn = MAIN.split("static bool shelly_meter_pump_only() {", 1)[1].split("}", 1)[0]
        self.assertIn("shelly_may_feed_pump_model(site_settings_snapshot().shelly_scope)", fn)
        # No other path claims a pump-only meter.
        self.assertEqual(len(re.findall(r"meter_pump_only\s*=\s*true", MAIN)), 0)
        self.assertEqual(MAIN.count("meter_pump_only=shelly_meter_pump_only();"), 2)
        # A different meter restarts the electric fit.
        self.assertIn("shelly_meter_identity()", WORKER)
        self.assertIn("id->reset_electric();", WORKER)
        # Editing the URL withdraws the confirmation (settings layer).
        self.assertIn("s.shelly_scope=ShellyScope::Unknown", SETTINGS)


class ComfortLimits(unittest.TestCase):
    def test_owner_limits_reach_planner_arbiter_and_price_controller(self):
        fn = MAIN.split("static ComfortLimits owner_comfort_limits() {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("site.comfort_min_c", fn)
        self.assertIn("site.comfort_max_c", fn)
        self.assertIn("pi.limits=owner_comfort_limits();", WORKER)
        arb = MAIN.split("static ArbiterInput build_arbiter_input(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("in.limits=owner_comfort_limits();", arb)
        # The fallback controller keeps applying the same limits, and refuses a
        # proposal when they cannot hold the target.
        boot = MAIN.split("static BootstrapDecision bootstrap_preview(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("mode_comfort_limits_fit(site,mode)", boot)
        self.assertIn("apply_profile(input,site.profile,site.comfort_min_c,site.comfort_max_c", boot)


class TimelineAndHistory(unittest.TestCase):
    def test_timeline_shows_the_arbitrated_decision(self):
        tl = MAIN.split("static void timeline_endpoint() {", 1)[1].split("static void prices_endpoint()", 1)[0]
        self.assertIn("active_control_proposal(now,indoor,&arbitration)", tl)
        self.assertNotIn("bootstrap_preview(", tl)
        self.assertIn("meta.decision_source=proposal_source_name(arbitration.source);", tl)
        self.assertIn('arbitration.source==ProposalSource::Mpc?"MPC_PLAN"', tl)

    def test_history_records_which_controller_acted_without_touching_the_record(self):
        collect = MAIN.split("static enevina::cottage::HistoryInputs history_collect() {", 1)[1].split(
            "static void freshness_endpoint()", 1)[0]
        self.assertIn("active_control_proposal(now,te,&arb)", collect)
        self.assertNotIn("bootstrap_preview(", collect)
        self.assertIn("in.control_source=static_cast<uint8_t>(arb.source);", collect)
        # Stored as an EVENT; the persisted 20-byte record is unchanged.
        self.assertIn("static_assert(sizeof(HistoryRecord)==20", HIST)
        self.assertIn("ControlSource=8", HIST)
        self.assertIn("control_source_events(in,events);", HIST)
        self.assertIn("uint8_t control_source=255;", HIST)


class SafetyChainUnchanged(unittest.TestCase):
    def test_gates_before_any_proposal(self):
        order = [
            "if(!cn105_selected())",
            "if(!control_armed.load())return;",
            "control_armed_endpoint.load()!=site_settings_endpoint_generation()",
            "!c23_config_storage_verified()",
            "active_control_proposal(now,sample,&arbitration)",
            "choose_mitsubishi_control(candidate)",
        ]
        last = -1
        for token in order:
            pos = TICK.find(token)
            self.assertGreater(pos, last, token)
            last = pos
        self.assertIn("candidate.ms_since_mode_change=control_ms_since_mode_change();", TICK)

    def test_sensor_rebinding_invalidates_model_and_plans(self):
        bind = AIR.split("if(binding_changed) {", 1)[1].split("log_event(4", 1)[0]
        self.assertIn("++config_generation;", bind)
        ident = MAIN.split("static uint32_t identifier_identity() {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("c23_indoor_binding_id()", ident)

    def test_settings_and_ota_protections_still_present(self):
        self.assertIn("kSettingsLayoutBoundaries", SETTINGS)
        self.assertIn("site_rollback_v2_modified", SETTINGS)
        self.assertIn("site_comfort_transaction_pending", AIR)
        self.assertIn("cottage_ota_network_maintenance()", MAIN)
        # The MPC added no flashing, restart or watchdog machinery.
        for token in ("esp_task_wdt_add", "esp_restart", "esp_ota_set_boot_partition"):
            self.assertNotIn(token, WORKER)


if __name__ == "__main__":
    unittest.main()
