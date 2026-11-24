"""Codex P2 regressions: never hide a permanently silent ESP32 serial stream."""
from pathlib import Path
from unittest import TestCase, mock
import tempfile, types
import importlib.util

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("cottage_stability",ROOT/"tools/cottage_usb_stability.py")
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class FakeClock:
    def __init__(self): self.now=0.0
    def monotonic(self):
        self.now += 15.0
        return self.now

class NoBytesSerial:
    def __init__(self,*args,**kwargs): self.dtr=False;self.rts=False
    def __enter__(self): return self
    def __exit__(self,*args): return False
    def readline(self): return b""

class HeartbeatGapTests(TestCase):
    def test_initial_and_trailing_silence(self):
        d=module.HeartbeatDeadline(0)
        self.assertIsNone(d.check(90))
        self.assertEqual(d.check(91),91)
        self.assertIsNone(d.check(200))
        d.beat(201)
        self.assertIsNone(d.check(290))
        self.assertEqual(d.check(292),91)
    def test_stuck_serial_still_reports_gap(self):
        clock=FakeClock()
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(module,"serial",types.SimpleNamespace(Serial=NoBytesSerial,SerialException=OSError)),mock.patch.object(module.time,"monotonic",clock.monotonic):
                result=module.monitor("mock-port",Path(tmp)/"capture.jsonl",210)
            self.assertGreaterEqual(result["heartbeat_gap"],1)
            self.assertEqual(result["heartbeat"],0)
            self.assertEqual(result["serial_error"],0)
    def test_plan_source_invalidated_on_mode_switch(self):
        main=(ROOT/"firmware/cottage/src/main.cpp").read_text()
        self.assertIn("if(verified.mode!=pump.mode || !continuous || heating_target_changed)",main)
        self.assertIn("plan_count=0;\n                last_plan.status=MpcStatus::InvalidInput;",main)
        self.assertIn("cn105_gateway_requests.snapshot(now,ms,endpoint,link)",main)

    def test_solver_result_cannot_resurrect_pretransition_mode(self):
        main=(ROOT/"firmware/cottage/src/main.cpp").read_text()
        self.assertIn("++observed_mode_generation;",main)
        self.assertIn("const auto view=boundary?optimizer_pump_state(now,midea)",main)
        self.assertIn("const bool mode_changed=after.request_generation!=view.request_generation",main)
        self.assertIn("after.link_generation!=view.link_generation",main)
        self.assertIn("cn105_learning_request(cn105_gateway_requests,",main)
        self.assertIn("slot.pump_start,view,commands)",main)
        assert main.index("if(mode_changed) {") < main.index("last_plan=result;")
        assert main.index("last_plan=result;") < main.index("xSemaphoreGive(lock_data);",main.index("if(mode_changed) {"))

    def test_sse_drop_invalidates_cached_and_in_flight_plan(self):
        main=(ROOT/"firmware/cottage/src/main.cpp").read_text()
        drop=main.index("observed_mode_duration.invalidate();")
        guarded=main[drop:main.index("xSemaphoreGive(lock_data);",drop)]
        self.assertIn("++observed_mode_generation;",guarded)
        self.assertIn("plan_count=0;",guarded)
        self.assertIn("last_plan.status=MpcStatus::InvalidInput;",guarded)

    def test_all_verified_source_failures_revoke_cached_and_in_flight_plan(self):
        # A new CN105 proof envelope introduced additional loss-of-trust
        # branches. Every dwell invalidation must also revoke the MPC plan.
        main=(ROOT/"firmware/cottage/src/main.cpp").read_text()
        token="observed_mode_duration.invalidate();"
        self.assertEqual(main.count(token),4)
        from_index=0
        for _ in range(4):
            at=main.index(token,from_index)
            until=main.index("xSemaphoreGive(lock_data);",at)
            guarded=main[at:until]
            self.assertIn("++observed_mode_generation;",guarded)
            self.assertIn("plan_count=0;",guarded)
            self.assertIn("last_plan.status=MpcStatus::InvalidInput;",guarded)
            from_index=until+1

    def test_first_verified_new_read_revokes_legacy_mode_plan(self):
        main=(ROOT/"firmware/cottage/src/main.cpp").read_text()
        start=main.index("if(proof==enevina::PumpReadProofStatus::NewRead)")
        end=main.index("pump=verified;",start)
        clause=main[start:end]
        self.assertIn("if(verified.mode!=pump.mode || !continuous || heating_target_changed)",clause)
        self.assertIn("++observed_mode_generation;",clause)
        self.assertIn("last_plan.status=MpcStatus::InvalidInput;",clause)

    def test_climate_gap_invalidates_plan_even_if_mode_unchanged(self):
        main=(ROOT/"firmware/cottage/src/main.cpp").read_text()
        start=main.index("if(proof==enevina::PumpReadProofStatus::NewRead)")
        event=main[start:main.index("pump=verified;",start)]
        self.assertIn("verified_cn105_link.current() && valid_cn105(pump,now)",event)
        self.assertIn("!continuous || heating_target_changed",event)
        self.assertIn("++observed_mode_generation;",event)
        self.assertIn("plan_count=0;",event)
        self.assertIn("last_plan.status=MpcStatus::InvalidInput;",event)
        self.assertLess(event.index("++observed_mode_generation;"),
                        event.index("observed_mode_duration.observe(")
                        if "observed_mode_duration.observe(" in event else len(event))
        # Source-level scenario: HEAT before/after the event, gap=181s.
        same_mode=True
        for gap,expected_invalid in [(179,False),(180,False),(181,True),(900,True)]:
            continuous=(gap<=180)
            self.assertEqual(not same_mode or not continuous,expected_invalid)

if __name__=="__main__":
    import unittest;unittest.main()
