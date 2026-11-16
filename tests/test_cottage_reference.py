"""Regression tests for the independent C0 SciPy MILP reference."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from cottage_reference import scenario, replay, solve


class CottageMilpTests(unittest.TestCase):
    def test_initial_on_unserved_dwell_keeps_heating(self):
        d = scenario("mild", 8)
        d["initial_on"] = True
        d["initial_minutes"] = 0
        d["min_heat_min"] = 60
        d["slots"] = [(10., 5., 0., 1.)] * 8
        result = solve(d, time_limit=5)
        self.assertTrue(result.get("feasible"), result)
        self.assertEqual(result["actions"][:4], [1, 1, 1, 1])
        self.assertFalse(replay(d, result["actions"])["violations"])

    def test_initial_off_unserved_dwell_keeps_off(self):
        d = scenario("mild", 8)
        d["initial_on"] = False
        d["initial_minutes"] = 0
        d["min_off_min"] = 60
        d["slots"] = [(10., 0.5, 0., 1.)] * 8
        result = solve(d, time_limit=5)
        self.assertTrue(result.get("feasible"), result)
        self.assertEqual(result["actions"][:4], [0, 0, 0, 0])
        self.assertFalse(replay(d, result["actions"])["violations"])


if __name__ == "__main__":
    unittest.main()
