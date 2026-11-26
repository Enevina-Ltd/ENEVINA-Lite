"""Source-level wiring checks, not physical pump evidence."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
FW=ROOT/'firmware/cottage'
class CommandReconciliationWiring(unittest.TestCase):
    def test_all_network_adapters(self):
        for name in ('echonet','gree','daikin','wf_rac','midea'):
            with self.subTest(adapter=name):
                s=(FW/'src'/f'cottage_{name}_service.cpp').read_text()
                self.assertIn('automatic_command_reconciliation.hpp',s)
                self.assertIn('reconciliation.awaiting(',s)
                self.assertIn('reconciliation.observe(',s)
                self.assertNotIn('reconciliation.allow(',s)
                self.assertIn('reconciliation.unresolved(',s)
                self.assertIn('ControlBlock::AwaitingReadback',s)
                self.assertIn('automatic &&',s)
    def test_serial_family(self):
        s=(FW/'include/cottage_serial_heating_service.hpp').read_text()
        self.assertIn('state.reconciliation.awaiting(',s)
        self.assertIn('state.reconciliation.observe(',s)
        self.assertIn('state.reconciliation.unresolved(',s)
        self.assertIn('outcome.attempted &&',s)
        self.assertIn('!outcome.confirmed',s)
        for name in ('cn105_direct','daikin_s21','haier','panasonic'):
            self.assertIn('cottage_serial_heating_service.hpp',(FW/'src'/f'cottage_{name}_service.cpp').read_text())
    def test_gateway_dispatch_after_preflight(self):
        s=(FW/'src/main.cpp').read_text()
        begin=s.index('if(dispatch) {',s.index('static bool cn105_bridge_post'))
        self.assertLess(begin,s.index('cn105_reconciliation.unresolved(',begin))
        self.assertLess(s.index('cn105_reconciliation.unresolved(',begin),s.index('req.POST("")',begin))
        self.assertIn('cn105_reconciliation.observe(cottage_control_authorization_generation(),true,',s)
        self.assertIn('if(bridge.command_unconfirmed)',s)
        self.assertIn('if(!sent && post_stage!=6 && post_stage!=-6)return;',s)
        self.assertIn('read_mode==bridge.mode && std::fabs(read_setpoint-decision.target_c)',s)
    def test_learning_rejects_unresolved_dispatches(self):
        main=(FW/'src/main.cpp').read_text()
        self.assertIn('!state.command_unconfirmed',main)
        self.assertIn('quiet_common_learning_request(slot.pump_start,view)',main)
        self.assertIn('slot.pump_start=optimizer_pump_state',main)
        for name in ('echonet','gree','daikin','wf_rac','midea'):
            with self.subTest(adapter=name):
                s=(FW/'src'/f'cottage_{name}_service.cpp').read_text()
                self.assertIn('.command_unconfirmed=',s)
                self.assertIn('.write_in_progress=',s)
                self.assertIn('.write_generation=',s)
                self.assertIn('++write_generation;' if name=='midea' else '++state.write_generation;',s)
                self.assertIn('cottage_control_authorization_generation()',s)
        self.assertIn('.command_unconfirmed=',(FW/'include/cottage_serial_heating_service.hpp').read_text())
    def test_uncertainty_is_reconciled_only_by_a_new_active_poll(self):
        for name in ('gree','daikin','echonet','wf_rac'):
            with self.subTest(adapter=name):
                s=(FW/'src'/f'cottage_{name}_service.cpp').read_text()
                tick=s.split(f'void cottage_{name}_tick()',1)[1]
                self.assertEqual(s.count('state.reconciliation.observe('),1)
                poll=tick.rsplit('job.work==Work::Read)' if name=='gree' else 'job.task==Task::Read)',1)[1]
                self.assertIn('!job.draft && selected && state.reconciliation.observe(',poll)
                self.assertIn('++state.request_generation;',poll)
                self.assertIn('state.automatic_queued_utc=state.automatic_confirmed_utc=0;',poll)
                self.assertLess(tick.index('state.write_active='),tick.index('state.write_active=false;'))
        serial=(FW/'include/cottage_serial_heating_service.hpp').read_text()
        self.assertEqual(serial.count('state.reconciliation.observe('),1)
        self.assertIn('!explicit_test && is_selected && state.reconciliation.observe(',serial)
        self.assertIn('state.write_active=job.command.has_value();',serial)
        midea=(FW/'src/cottage_midea_service.cpp').read_text()
        self.assertEqual(midea.count('reconciliation.observe('),1)
        self.assertIn('const bool reconciled=!action && reconciliation.observe(',midea)
        self.assertIn('!reconciled && fresh_locked(',midea)
        main=(FW/'src/main.cpp').read_text()
        room=main.split('static enevina::PumpRoomState cn105_room_state()',1)[1].split('void cottage_midea_automatic_cancel()',1)[0]
        self.assertEqual(main.count('cn105_reconciliation.observe('),1)
        self.assertIn('cn105_reconciliation.observe(',room)
        self.assertIn('cn105_gateway_requests.invalidate();',room)
        self.assertNotIn('control_commands_confirmed.fetch_add',room)
        self.assertNotIn('control_last_ack_utc.store',room)
    def test_gree_failed_preflight_withdraws_state(self):
        s=(FW/'src/cottage_gree_service.cpp').read_text()
        self.assertIn('outcome.observed=true;',s)
        self.assertIn('if(command.observed)publish_reading(command.reading,job.link)',s)
        self.assertIn('readback_matches(request.patch,before,*after) && command_current(job)',s)
        self.assertIn('state.automatic_confirmed_utc=state.automatic_queued_utc=0;',s)
        self.assertIn('request.previous=*state.sample.reading;',s)
    def test_wired_thermostat_does_not_require_wifi_or_utc(self):
        main=(FW/'src/main.cpp').read_text()
        serial=(FW/'include/cottage_serial_heating_service.hpp').read_text()
        link=serial.split('struct Link {',1)[1].split('struct Sample',1)[0]
        self.assertNotIn('cottage_wifi_link_generation',link)
        self.assertIn('endpoint==site_settings_endpoint_generation()',link)
        self.assertIn('sample.bus_generation!=cottage_pump_uart.generation()',serial)
        self.assertIn('out.local_bus_sample=true;out.local_capture_ms=sample.received_ms;',serial)
        self.assertIn('if(sample.received_utc>=1700000000)',serial)
        self.assertIn('return wired_pump_selected() || (wifi_ok.load() && clock_ok());',main)
        self.assertIn('wired_pump_selected() && valid_local_temperature(t,millis())',main)
        self.assertIn('!wifi_ok.load() || !clock_ok() || c23_mode_config().mode==CottageMode::Drying)in.plan={};',main)
        self.assertIn('pi.temperature_fresh=valid_temperature(ts,now)',main)
    def test_explicit_cold_requires_verified_off_readback(self):
        main=(FW/'src/main.cpp').read_text()
        self.assertIn('candidate.explicit_cold=mode.mode==CottageMode::Cold;',main)
        self.assertIn('automatic_mode_eligible(bridge.mode,candidate.explicit_cold)',main)
        self.assertIn('bridge_mode==enevina::PumpRoomMode::Other && step!=ControlStep::SetOff',main)
        self.assertIn('read_mode==enevina::PumpRoomMode::Off;',main)
        for name in ('echonet','gree','daikin','wf_rac','midea'):
            service=(FW/'src'/f'cottage_{name}_service.cpp').read_text()
            self.assertIn('automatic_mode_eligible(',service)
            self.assertIn('current_other=',service)
        self.assertIn('automatic_mode_eligible(mode,proposal.explicit_cold)',(FW/'include/cottage_serial_heating_service.hpp').read_text())
    def test_user_can_see_unconfirmed_status(self):
        s=(FW/'include/cottage_premium_dashboard.hpp').read_text()
        self.assertIn("gate==='AWAITING_READBACK'",s)
if __name__=='__main__':unittest.main()
