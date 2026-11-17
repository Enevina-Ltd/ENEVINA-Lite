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
        self.assertIn("if(copy.mode!=pump.mode)",main)
        self.assertIn("plan_count=0;\n                last_plan.status=MpcStatus::InvalidInput;",main)
        self.assertIn("cp.mode==ObservedPumpMode::Heat || cp.mode==ObservedPumpMode::Off",main)

if __name__=="__main__":
    import unittest;unittest.main()
