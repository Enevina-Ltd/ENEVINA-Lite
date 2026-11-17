import importlib.util
from pathlib import Path
import unittest
SOURCE=Path(__file__).resolve().parents[1]/"tools"/"cottage_usb_stability.py"
spec=importlib.util.spec_from_file_location("stability",SOURCE)
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class TestUsbLogger(unittest.TestCase):
    def test_classification(self):
        self.assertEqual(mod.classify("rst:0x1 (POWERON)"),"boot")
        self.assertEqual(mod.classify("C2_SHADOW wifi=0 clock=0 plan_slots=0"),"heartbeat")
        self.assertEqual(mod.classify("ENEVINA LITE C2 SHADOW: NO ACTUATOR PATH"),"shadow_start")
        self.assertEqual(mod.classify("C2_SHADOW_FATAL: expected PSRAM"),"fatal")
        self.assertEqual(mod.classify("Task watchdog got triggered"),"watchdog")
        self.assertIsNone(mod.classify("ESP32 initial boot"))
    def test_source_is_readonly(self):
        s=SOURCE.read_text()
        self.assertNotIn(".write(",s.split("with serial.Serial(")[1].split("except ")[0])
        self.assertIn("time.monotonic()",s)
        self.assertIn("os.umask(0o077)",s)

if __name__=="__main__":
    unittest.main()
