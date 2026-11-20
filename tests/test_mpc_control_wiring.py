"""Source guards for the live MPC chain in firmware (NOT a hardware proof).

Pins: MPC -> price -> thermostat order through ONE arbitrated proposal, the
unchanged arming/adapter/dwell gates, readback-only "executed" claims, honest
JSON (no NaN, energy only when trusted), the meter-scope rule on main and the
absence of new watchdog/restart machinery.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
CORE = (ROOT / "firmware/cottage/src/core.cpp").read_text(encoding="utf-8")
ARB = (ROOT / "cottage/mpc_arbiter.hpp").read_text(encoding="utf-8")
TICK = MAIN.split("static void cottage_control_tick() {", 1)[1].split(
    "static void cottage_control_registration()", 1)[0]
POST = MAIN.split("static bool cn105_bridge_post(", 1)[1].split(
    "static void cottage_control_tick()", 1)[0]
WORKER = MAIN.split("static void optimizer_worker(void*) {", 1)[1].split(
    "static void status_endpoint()", 1)[0]


class MpcControlWiring(unittest.TestCase):
    def test_one_arbitrated_proposal_for_tick_and_post(self):
        self.assertIn("const auto proposal=active_control_proposal(now,sample,&arbitration);", TICK)
        self.assertNotIn("bootstrap_preview(", TICK)
        self.assertIn("const auto latest=active_control_proposal(now,current_temp);", POST)
        self.assertIn("const auto refreshed=active_control_proposal(", POST)
        self.assertNotIn("bootstrap_preview(", POST)
        # The arbiter falls back to the existing price/thermostat decision.
        self.assertIn("out.proposal=in.bootstrap;", ARB)
        self.assertIn("in.bootstrap=bootstrap_preview(now,t);", MAIN)

    def test_existing_gates_unchanged_and_before_any_command(self):
        # Midea / read-only adapters never reach the CN105 writer.
        self.assertLess(TICK.index("if(!cn105_selected())"),
                        TICK.index("active_control_proposal("))
        self.assertLess(TICK.index("if(!control_armed.load())return;"),
                        TICK.index("active_control_proposal("))
        self.assertIn("const auto decision=choose_mitsubishi_control(candidate);", TICK)
        self.assertIn("candidate.ms_since_mode_change=control_ms_since_mode_change();", TICK)
        # The MPC never adds a write path of its own.
        self.assertNotIn("cn105_bridge_post(", WORKER)
        self.assertNotIn("req.POST", WORKER)
        self.assertEqual(MAIN.count('req.POST("")'), 1)

    def test_executed_only_after_readback(self):
        stores = [m.start() for m in re.finditer(r"mpc_confirmed_slot_utc\.store\(", MAIN)]
        self.assertEqual(len(stores), 2)
        self.assertGreater(stores[0], TICK and MAIN.index("decision.gate==ControlBlock::AlreadyApplied"))
        confirmed = MAIN.index("control_commands_confirmed.fetch_add(1);")
        self.assertGreater(stores[1], confirmed)
        self.assertIn("planned_heat?(last_heating && std::fabs(last_setpoint-16.0)<0.01):!last_heating", MAIN)

    def test_plan_bound_to_its_slot_and_calibration(self):
        self.assertIn("record.first_utc+900", MAIN)
        self.assertIn("calibration_snapshot.state==CalibrationState::Calibrated", MAIN)
        self.assertIn("in.now_utc>=p.first_utc+900", ARB)
        self.assertIn("pi.controls.runtime_budget_ms=1800;", WORKER)
        self.assertIn("pi.controls.cancel_requested=cottage_cancel;", WORKER)

    def test_no_nan_or_untrusted_energy_in_json(self):
        plan = MAIN.split("static void plan_endpoint() {", 1)[1].split("static void mpc_endpoint() {", 1)[0]
        self.assertIn('strlcpy(kwh,"null",sizeof(kwh));', plan)
        self.assertIn('strlcpy(eur,"null",sizeof(eur));', plan)
        self.assertNotIn('\\"kwh\\":%.6f', plan)
        mpc = MAIN.split("static void mpc_endpoint() {", 1)[1].split("static void weather_forecast_endpoint()", 1)[0]
        self.assertIn("const bool energy_ok=plan_ok && energy;", mpc)
        self.assertIn('\\"cop_identified\\":false', mpc)

    def test_pump_state_only_from_verified_source(self):
        # Codex P1 (PR #63): a gateway GET/SSE receipt may repeat cached
        # state; learning labels, planning state and plan validity use only
        # a fresh source-verified CN105 poll.
        view = MAIN.split("static PumpView pump_view(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("cp.evidence.source_time_verified", view)
        self.assertIn("valid_cn105(cp,now)", view)
        self.assertNotIn("cn105_bridge_get", view)
        self.assertNotIn("cn105_bridge_get", WORKER)
        self.assertIn("o.heat_valid=view.known && view.minutes>=14 &&", WORKER)
        self.assertIn("o.heat_requested=view.heat;", WORKER)
        arb = MAIN.split("static ArbiterInput build_arbiter_input(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("const PumpView view=pump_view(cp,now,minutes);", arb)
        self.assertIn("mpc_confirmed_slot_utc.load()==in.plan.first_utc", arb)
        self.assertNotIn("bridge_observed", MAIN)

    def test_model_bound_to_indoor_sensor_and_plan_display_uses_controller_validity(self):
        # Codex P2 #1 (PR #63): the learned model describes the room the bound
        # RuuviTag measures; re-binding resets learning and revokes plans.
        ident = MAIN.split("static uint32_t identifier_identity() {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("c23_indoor_binding_id()", ident)
        air = (ROOT / "firmware/cottage/src/cottage_air_service.cpp").read_text(encoding="utf-8")
        bind = air.split("if(binding_changed) {", 1)[1].split("log_event(4", 1)[0]
        self.assertIn("++config_generation;", bind)
        self.assertIn("uint32_t c23_indoor_binding_id() {", air)
        self.assertIn("slot.config_generation==c23_config_generation()", WORKER)
        self.assertIn("slot.config_generation=c23_config_generation();", WORKER)
        # Codex P2 #2: /api/plan and /api/mpc use the controller's validity.
        plan = MAIN.split("static void plan_endpoint() {", 1)[1].split("static void json_number_or_null", 1)[0]
        self.assertIn("plan_validity(", plan)
        self.assertIn("validity==MpcFallback::None", plan)
        mpc = MAIN.split("static void mpc_endpoint() {", 1)[1].split("static void weather_forecast_endpoint()", 1)[0]
        self.assertIn("plan_validity(arb_in)", mpc)
        self.assertIn("validity==MpcFallback::None", mpc)

    def test_meter_scope_rule_on_main(self):
        self.assertIn("static bool shelly_meter_pump_only() {return false;}", MAIN)
        self.assertIn("o.meter_pump_only=shelly_meter_pump_only();", WORKER)
        self.assertIn("pi.meter_pump_only=shelly_meter_pump_only();", WORKER)

    def test_learning_is_read_only_and_persisted_hourly(self):
        self.assertIn('nvs_open("cottage_mdl",NVS_READWRITE,&handle)', MAIN)
        self.assertIn("++boundaries_since_save>=24 && revision!=saved_revision", WORKER)
        self.assertIn("stats.free_entries<2*entries+128", MAIN)
        self.assertIn("identifier_identity()", WORKER)
        self.assertIn('web.on("/api/model/reset",HTTP_POST', MAIN)
        reset = MAIN.split('web.on("/api/model/reset",HTTP_POST', 1)[1].split("});", 1)[0]
        self.assertIn('c23_authorize_local_setting(web.arg("token").c_str())', reset)

    def test_no_new_watchdog_or_restart_machinery(self):
        for token in ("esp_task_wdt_add", "esp_restart", "xTaskCreate"):
            self.assertNotIn(token, WORKER)
        self.assertIn('#include "../../../cottage/building_identification.cpp"', CORE)
        self.assertIn('#include "../../../cottage/mpc_planner.cpp"', CORE)


if __name__ == "__main__":
    unittest.main()
