"""Runtime regression: a HOLD must not be reported as failed control;
actionable HEAT/OFF still requires a fresh device GET, with bounded read retries.
"""
from pathlib import Path
import unittest
SRC=(Path(__file__).resolve().parents[1]/
     "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
class CN105Maintenance(unittest.TestCase):
    def test_hold_does_not_require_get_to_avoid_false_error(self):
        body=SRC.split("static void cottage_control_tick(const enevina::PumpRoomState& bridge) {",1)[1].split(
            "static void cottage_control_registration()",1)[0]
        marker="if(proposal.action==BootstrapAction::Hold) {"
        self.assertIn(marker,body)
        earlier=body[:body.index(marker)]
        self.assertIn("control_temperature_fresh(sample,now)",earlier)
        self.assertIn("proposal.action==BootstrapAction::Unavailable",earlier)
        later=body[body.index(marker):]
        self.assertIn("ControlBlock::NoProposal",later)
        worker=SRC.split("static void optimizer_worker(void*) {",1)[1]
        self.assertLess(worker.index("cn105_room_state()"),worker.index("cottage_control_tick(bridge);"))
        # HOLD is read-only/NoProposal. Actionable requests require the
        # bridge GET above to have succeeded on the current connection.
        self.assertLess(later.index("ControlBlock::NoProposal"),
                        later.index("if(bridge.source!=enevina::PumpStateSource::Gateway"))
        room=SRC.split("static enevina::PumpRoomState cn105_room_state() {",1)[1].split(
            "void cottage_midea_automatic_cancel()",1)[0]
        self.assertIn("if(!cn105_bridge_get(observed_mode,setpoint)",room)
        self.assertIn("control_note_observed_mode(observed_mode,{endpoint,link});",room)
        self.assertIn("PumpStateSource::Gateway",room)
        self.assertIn("ControlBlock::InputsUnverified",later)
        self.assertIn("choose_mitsubishi_control(candidate)",later)
    def test_get_memory_guard_and_bounded_retry(self):
        body=SRC.split("static bool cn105_bridge_get(",1)[1].split(
            "static bool cn105_bridge_post(",1)[0]
        self.assertIn("NetworkBudgetLease lease(5000,72*1024,16*1024)",body)
        self.assertIn("if(attempt+1<cn105_read_max_attempts)",body)
        self.assertIn("cn105_get_lease_denials.fetch_add(1)",body)
        self.assertIn("cn105_get_lease_low_memory.fetch_add(1)",body)
        self.assertIn("cn105_get_lease_busy.fetch_add(1)",body)
        self.assertIn("const auto& detail=lease.diagnostic()",body)
        self.assertIn("NetworkBudgetFailure::LowMemory",body)
        self.assertIn("NetworkBudgetFailure::MutexBusy",body)
        self.assertNotIn("heap_caps_get_free_size(",body)
        self.assertIn("cn105_get_retry_count.fetch_add(1)",body)
        self.assertIn("req.GET()",body)
        self.assertNotIn("req.POST(",body)
    def test_trace_fields_exposed(self):
        for name in ("cn105_get_lease_denials","cn105_get_lease_low_memory",
                     "cn105_get_lease_busy","cn105_get_last_free_internal",
                     "cn105_get_last_largest_internal"):
            self.assertIn('"'+name+'"',SRC)
if __name__=="__main__":unittest.main()
