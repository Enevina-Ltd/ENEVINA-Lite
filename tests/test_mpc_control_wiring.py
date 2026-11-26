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
TICK = MAIN.split("static void cottage_control_tick(const enevina::PumpRoomState& bridge) {", 1)[1].split(
    "static void cottage_control_registration()", 1)[0]
POST = MAIN.split("static bool cn105_bridge_post(", 1)[1].split(
    "static void cottage_control_tick(const enevina::PumpRoomState& bridge)", 1)[0]
WORKER = MAIN.split("static void optimizer_worker(void*) {", 1)[1].split(
    "static void status_endpoint()", 1)[0]


class MpcControlWiring(unittest.TestCase):
    def test_one_arbitrated_proposal_for_tick_and_post(self):
        self.assertIn("const auto proposal=active_control_proposal(now,sample,&arbitration);", TICK)
        self.assertNotIn("bootstrap_preview(", TICK)
        self.assertIn("const auto latest=active_control_proposal(after_get_utc,selected_room_sample(after_get_utc),&latest_detail);", POST)
        self.assertIn("const auto refreshed=active_control_proposal(", POST)
        self.assertNotIn("bootstrap_preview(", POST)
        # The arbiter falls back to the existing price/thermostat decision.
        self.assertIn("out.proposal=in.bootstrap;", ARB)
        self.assertIn("in.bootstrap=bootstrap_preview(now,t,state.heating);", MAIN)

    def test_direct_lan_history_and_freshness_use_the_selected_adapter(self):
        pump = MAIN.split("static enevina::PumpRoomState optimizer_pump_state(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("if(cottage_echonet_selected())return cottage_echonet_room_state();", pump)
        self.assertIn("if(cottage_wf_rac_selected())return cottage_wf_rac_room_state();", pump)
        self.assertIn("if(cottage_daikin_dsiot_selected())return cottage_daikin_dsiot_room_state();", pump)
        self.assertIn("if(cottage_cn105_direct_selected())return cottage_cn105_direct_room_state();", pump)
        self.assertIn("if(cottage_daikin_s21_selected())return cottage_daikin_s21_room_state();", pump)
        for function in ("static enevina::cottage::HistoryInputs history_collect()",
                         "static void freshness_endpoint()"):
            body = MAIN.split(function, 1)[1].split("\n}\n", 1)[0]
            self.assertIn("if(uses_common_pump_state())", body)
            self.assertIn("optimizer_pump_state(now,midea)", body)

    def test_dsiot_cached_module_status_is_display_only(self):
        source = (ROOT / "firmware/cottage/src/cottage_daikin_dsiot_service.cpp").read_text(encoding="utf-8")
        state = source.split("cottage_daikin_dsiot_room_state() {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("PumpStateSource::Gateway", state)
        self.assertNotIn("PumpStateSource::DirectLan", state)
        self.assertNotIn("result.indoor_c", state)
        self.assertNotIn("result.mode", state)
        self.assertIn('cJSON_AddBoolToObject(root,"pump_side_fresh",false)', source)

    def test_existing_gates_unchanged_and_before_any_command(self):
        # Other adapters cannot reach the CN105 writer.
        self.assertLess(TICK.index("if(!midea && !gree && !daikin && !echonet && !wf_rac && !direct_cn105 && !s21 && !haier && !panasonic && !cn105_selected())"),
                        TICK.index("active_control_proposal("))
        self.assertLess(TICK.index("if(!control_armed.load())return;"),
                        TICK.index("active_control_proposal("))
        self.assertIn("const auto decision=choose_mitsubishi_control(candidate);", TICK)
        self.assertLess(TICK.index("cottage_midea_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertIn("site_settings_generation()!=site_generation", MAIN)
        self.assertIn("in.pump_state_known=known_heating_mode(state);", MAIN)
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
        self.assertIn("planned_heat?(last_heating && std::fabs(last_setpoint-proposal.heat_setpoint_c)<0.01):!last_heating", MAIN)

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

    def test_pump_state_from_standard_gateway_request_or_native_adapter(self):
        # A gateway GET may repeat cached pump state. Treat it only as a
        # request for an independently validated input-response model.
        view = MAIN.split("static enevina::PumpRoomState optimizer_pump_state(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("cn105_gateway_requests.snapshot(now,ms,endpoint,link)", view)
        self.assertNotIn("sse_observed_pump", view)
        self.assertNotIn("PumpStateSource::VerifiedBus", view)
        self.assertIn("cn105_learning_request(cn105_gateway_requests,", WORKER)
        self.assertIn("slot.pump_start,view,commands)", WORKER)
        self.assertIn("commands.confirmed=control_commands_confirmed.load()-slot.commands_confirmed", WORKER)
        self.assertIn("commands.failed=control_commands_failed.load()-slot.commands_failed", WORKER)
        self.assertIn("commands.ack_utc=cn105_history_ack.utc", WORKER)
        self.assertIn("cn105-gateway-request-v2", MAIN)
        self.assertIn("cn105_gateway_requests.note(heating,target,utc,ms,link.endpoint,link.wifi)", MAIN)
        self.assertIn("cn105_gateway_requests.invalidate()", MAIN)
        self.assertIn("slot.pump_start=optimizer_pump_state(time(nullptr),false)", WORKER)
        self.assertLess(WORKER.index("cn105_room_state()"),WORKER.index("const auto request=optimizer_pump_state(now,midea)"))
        self.assertLess(WORKER.index("const auto request=optimizer_pump_state(now,midea)"),WORKER.index("cottage_control_tick(bridge);"))
        self.assertIn("if(midea)return cottage_midea_room_state();", view)
        self.assertIn("if(cottage_gree_selected())return cottage_gree_room_state();", view)
        self.assertLess(TICK.index("cottage_gree_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertIn("if(cottage_daikin_selected())return cottage_daikin_room_state();", view)
        self.assertLess(TICK.index("cottage_daikin_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertLess(TICK.index("cottage_echonet_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertLess(TICK.index("cottage_wf_rac_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertLess(TICK.index("cottage_cn105_direct_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertLess(TICK.index("cottage_daikin_s21_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertLess(TICK.index("cottage_haier_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertLess(TICK.index("cottage_panasonic_submit_automatic("), TICK.index("cn105_bridge_post("))
        self.assertNotIn("cn105_bridge_get", view)
        self.assertNotIn("cn105_bridge_get", WORKER)
        self.assertIn("quiet_common_learning_request(slot.pump_start,view)", WORKER)
        self.assertIn("o.heat_requested=view_heat;", WORKER)
        arb = MAIN.split("static ArbiterInput build_arbiter_input(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("const auto state=optimizer_pump_state(now,midea);", arb)
        self.assertIn("mpc_confirmed_slot_utc.load()==in.plan.first_utc", arb)
        self.assertIn("state.automatic_queued_utc>=in.plan.computed_utc", arb)
        self.assertIn("state.automatic_confirmed_utc<=now", arb)
        self.assertNotIn("bridge_observed", MAIN)

    def test_defrost_gateway_protection_uses_standard_optional_entities(self):
        self.assertIn('text_sensor/Sub Mode', MAIN)
        self.assertIn('/text_sensor/Sub%20Mode?detail=all', MAIN)
        self.assertIn('cn105_action_signal', MAIN)
        self.assertIn('read_started_ms', MAIN)
        self.assertIn('cn105_read_optional_submode(link);', POST)
        dispatch = POST.split('const bool dispatch=',1)[1].split('if(dispatch)',1)[0]
        self.assertTrue(dispatch.strip().startswith('!defrost_hold &&'))
        self.assertIn('control_armed.load() &&', dispatch)
        self.assertIn('mode_change_allowed(step,control_ms_since_mode_change(),mitsubishi_min_mode_dwell_ms,refreshed,true) &&', dispatch)
        self.assertLess(POST.index('const bool defrost_hold=cn105_defrost_defer_write'),
                        POST.index('const int code=req.POST("");'))
        self.assertIn('if(!sent && post_stage!=6 && post_stage!=-6)return;', TICK)
        self.assertIn('o.defrost_observed=slot.defrost_at_start', WORKER)
        self.assertIn('defrost.generation!=slot.defrost_reports', WORKER)
        self.assertIn('pi.defrost_reports=defrost.generation', WORKER)
        self.assertIn('in.pump_defrost_hold=cn105_defrost_defer_write', MAIN)
        self.assertIn('p.defrost_reports!=in.defrost_reports', ARB)
        self.assertIn('cn105_defrost_uart_verified",false', MAIN)
        self.assertIn('mitsubishi_min_mode_dwell_ms', POST)

    def test_panasonic_requires_explicit_pump_bus_evidence(self):
        source = (ROOT / "firmware/cottage/src/cottage_panasonic_service.cpp").read_text(encoding="utf-8")
        setup = source.split("static bool connect(const Link& link)", 1)[1].split("static bool fill(", 1)[0]
        self.assertLess(setup.index("if(const auto wlan=handshake(link))return *wlan;"),
                        setup.index("family=wire::Family::Cnt;"))
        self.assertEqual(setup.count("cnt_query(link)"), 2)
        self.assertIn("p.sequence()!=sequence",
                      (ROOT / "pump/panasonic_uart.cpp").read_text(encoding="utf-8"))

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
        self.assertIn("slot.config_generation=optimizer_config_generation;", WORKER)
        # Codex P2 #2: /api/plan and /api/mpc use the controller's validity.
        plan = MAIN.split("static void plan_endpoint() {", 1)[1].split("static void json_number_or_null", 1)[0]
        self.assertIn("plan_validity(", plan)
        self.assertIn("validity==MpcFallback::None", plan)
        mpc = MAIN.split("static void mpc_endpoint() {", 1)[1].split("static void weather_forecast_endpoint()", 1)[0]
        self.assertIn("plan_validity(arb_in)", mpc)
        self.assertIn("validity==MpcFallback::None", mpc)

    def test_model_reset_is_verified_and_never_claimed_early(self):
        # Codex P2 (PR #63): an NVS failure must not let the old calibration
        # return after a reboot, and the owner is never told it is permanent.
        self.assertNotIn("identifier_erase", MAIN)
        self.assertIn("persist_model_reset(model_store,model_epoch,epoch_known)", WORKER)
        self.assertIn("load_model_epoch(model_store,model_epoch)", WORKER)
        self.assertIn("model_store_identity(identity,model_epoch)", WORKER)
        # Restoring needs a known epoch; saving never uses an unknown one.
        self.assertIn("epoch_known &&", WORKER)
        self.assertIn("ModelStore::Failed", WORKER)
        reset = MAIN.split('web.on("/api/model/reset",HTTP_POST', 1)[1].split("});", 1)[0]
        self.assertIn('\\"persisted\\":false', reset)
        self.assertNotIn('\\"persisted\\":true', reset)
        # A failed persist is retried and surfaced in /api/mpc.
        self.assertIn("reset_unpersisted=persist_model_reset(model_store,model_epoch,epoch_known)", WORKER)
        self.assertIn("identifier_reset_state.store(reset_unpersisted?3:2);", WORKER)
        self.assertIn('\\"reset_state\\":\\"%s\\"', MAIN)
        # The adapter verifies instead of trusting the erase call.
        store = MAIN.split("struct NvsModelStore {", 1)[1].split("\n};\n", 1)[0]
        self.assertIn('nvs_erase_key(handle,model_blob_key(variable))', store)
        self.assertIn("nvs_commit(handle)", store)
        self.assertIn("nvs_get_blob(handle,model_blob_key(variable),nullptr,&n)", store)
        self.assertIn("nvs_set_u32(handle,epoch_key(),value)", store)
        self.assertIn('variable?"rls2":"rls1"', MAIN)
        self.assertIn('variable?"epoch2":"epoch"', store)
        # The existing local setting token guard is unchanged: no new auth.
        self.assertIn('c23_authorize_local_setting(web.arg("token").c_str())', reset)
        self.assertNotIn("password", reset.lower())
        self.assertNotIn("pin", reset.lower().replace("pinned", ""))

    def test_api_plan_serialises_the_same_record_as_api_mpc(self):
        # Codex P2 (PR #63): the legacy rows are cleared on every verified mode
        # change, including the plan's own acknowledged transition.
        plan = MAIN.split("static void plan_endpoint() {", 1)[1].split("static void json_number_or_null", 1)[0]
        self.assertIn("const MpcPlanRecord record=mpc_plan;", plan)
        self.assertIn("mpc_row_count", plan)
        self.assertIn("std::copy(mpc_rows,mpc_rows+n,rows_mem.get());", plan)
        self.assertIn("record.status==MpcStatus::Ok", plan)
        self.assertNotIn("plan_count", plan)
        self.assertNotIn("last_plan", plan)
        self.assertNotIn("plan_rows", plan)

    def test_owner_comfort_limits_reach_planner_and_controller(self):
        arb = MAIN.split("static ArbiterInput build_arbiter_input(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("in.limits=owner_comfort_limits();", arb)
        self.assertIn("pi.limits=owner_comfort_limits();", WORKER)
        self.assertIn("static ComfortLimits owner_comfort_limits()", MAIN)
        self.assertIn("mpc_comfort_band(p.target_c,band,in.limits,p.variable_setpoints,p.maintenance_storage)", ARB)
        self.assertIn("pi.maintenance_storage=pi.low_temperature_cycle", WORKER)
        self.assertIn("p.maintenance_storage!=(p.low_temperature_cycle && in.bootstrap.maintenance_storage)", ARB)
        self.assertNotIn("p.band.lower_c-0.2", ARB)       # no tolerance beyond the limit
        self.assertNotIn("p.band.upper_c+0.3", ARB)

    def test_owner_limits_come_from_the_profile_settings(self):
        fn = MAIN.split("static ComfortLimits owner_comfort_limits() {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("site.comfort_min_c", fn)
        self.assertIn("site.comfort_max_c", fn)
        boot = MAIN.split("static BootstrapDecision bootstrap_preview(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("mode_comfort_limits_fit(site,mode)", boot)
        self.assertIn("apply_profile(input,site.profile,site.comfort_min_c,site.comfort_max_c", boot)

    def test_meter_scope_uses_owner_confirmation_and_binds_both_reads(self):
        self.assertIn("confirmed_shelly_meter_identity(site_settings_snapshot())", MAIN)
        self.assertIn("confirmed_shelly_meter_identity(settings)", MAIN)
        self.assertIn("slot.energy_meter_identity==meter_id", WORKER)
        self.assertIn("meter.meter_identity==meter_id", WORKER)
        self.assertIn("pi.meter_pump_only=meter_id!=0;", WORKER)
        self.assertIn("id->select_electric_meter(shared_id?shared_id:meter_id,shared_id!=0);", WORKER)
        self.assertIn("confirmed_shared_meter_identity(meter_site)",WORKER)
        self.assertIn("meter.source_identity==shared_id",WORKER)
        self.assertIn("slot.energy_source_identity==shared_id",WORKER)
        self.assertIn("incremental_slot_energy(slot.baseload,base,shared_id",WORKER)
        self.assertIn("pi.meter_incremental=shared_id!=0 && baseload_current",WORKER)

    def test_learning_is_read_only_and_persisted_hourly(self):
        self.assertIn('nvs_open("cottage_mdl",NVS_READWRITE,&handle)', MAIN)
        self.assertIn("checkpoint.due(learned.samples,revision!=saved_revision)", WORKER)
        self.assertIn("learning_room_boundary(slot_start,ts.captured_utc,now)", WORKER)
        self.assertIn("learning_interval_span(slot.observed_utc,now,slot.observed_ms,observation_ms)", WORKER)
        self.assertIn("slot.site_generation==optimizer_site_generation", WORKER)
        self.assertIn("stats.free_entries<2*entries+128", MAIN)
        self.assertIn("enevina_firmware::history_model_write(id.variable_setpoints(),buf,used)",MAIN)
        self.assertIn("if(file!=CheckpointRead::Missing)return false;",MAIN)
        self.assertIn("enevina_firmware::history_model_erase(variable)",MAIN)
        self.assertIn("enevina_firmware::history_model_present(variable)",MAIN)
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
