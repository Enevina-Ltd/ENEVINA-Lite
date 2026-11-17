"""PR #7 P2: BLE scanning must release the RAM/TLS lease before sleep."""
from pathlib import Path
import unittest
import re
ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "firmware/cottage/src/cottage_air_service.cpp").read_text()


class LeaseLifetimeTests(unittest.TestCase):
    def test_lease_is_destroyed_before_ble_wait(self):
        section = SRC.split("void c23_ble_worker(void*) {", 1)[1].split("bool c23_ble_initialized()", 1)[0]
        self.assertRegex(section, re.compile(
            r"\{\s*// The network-memory lock.*?"
            r"NetworkBudgetLease lease\(.*?"
            r"scan->clearResults\(\);.*?"
            r"\}\s*// lease destructor releases network budget before waiting\s*"
            r"vTaskDelay\(pdMS_TO_TICKS\(12000\)\);", re.S))
        self.assertEqual(section.count("NetworkBudgetLease lease("), 1)


if __name__ == "__main__":
    unittest.main()
