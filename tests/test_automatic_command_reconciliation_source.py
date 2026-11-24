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
                self.assertIn('reconciliation.allow(',s)
                self.assertIn('reconciliation.unresolved(',s)
                self.assertIn('ControlBlock::AwaitingReadback',s)
                self.assertIn('automatic &&',s)
    def test_serial_family(self):
        s=(FW/'include/cottage_serial_heating_service.hpp').read_text()
        self.assertIn('state.reconciliation.allow(',s)
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
        self.assertIn('cn105_reconciliation.allow(authorization_generation,true,bridge.mode,bridge.setpoint_c)',s)
        self.assertIn('if(!sent && post_stage!=6 && post_stage!=-6)return;',s)
        self.assertIn('read_heating==current_heat && std::fabs(read_setpoint-decision.target_c)',s)
    def test_learning_rejects_unresolved_dispatches(self):
        main=(FW/'src/main.cpp').read_text()
        self.assertIn('!state.command_unconfirmed',main)
        self.assertIn('!slot.command_unconfirmed_at_start',main)
        self.assertIn('command_unconfirmed_at_start=optimizer_pump_state',main)
        for name in ('echonet','gree','daikin','wf_rac','midea'):
            with self.subTest(adapter=name):
                s=(FW/'src'/f'cottage_{name}_service.cpp').read_text()
                self.assertIn('.command_unconfirmed=',s)
                self.assertIn('cottage_control_authorization_generation()',s)
        self.assertIn('.command_unconfirmed=',(FW/'include/cottage_serial_heating_service.hpp').read_text())
    def test_gree_failed_preflight_withdraws_state(self):
        s=(FW/'src/cottage_gree_service.cpp').read_text()
        self.assertIn('outcome.observed=true;',s)
        self.assertIn('if(command.observed)publish_reading(command.reading,job.link)',s)
        self.assertIn('readback_matches(request.patch,before,*after) && command_current(job)',s)
        self.assertIn('state.automatic_confirmed_utc=state.automatic_queued_utc=0;',s)
        self.assertIn('request.previous=*state.sample.reading;',s)
    def test_user_can_see_unconfirmed_status(self):
        s=(FW/'include/cottage_premium_dashboard.hpp').read_text()
        self.assertIn("gate==='AWAITING_READBACK'",s)
if __name__=='__main__':unittest.main()
