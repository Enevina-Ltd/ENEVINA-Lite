import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from reference_benchmark import heating_curve, price_threshold, replay, scenario, solve_milp


class ReferenceTests(unittest.TestCase):
    def test_heating_curve_raises_supply_when_outdoor_temperature_falls(self):
        p, _ = scenario(2)
        warm = heating_curve(p, [(8, 0.2, 0, 0)] * 2)[0]
        cold = heating_curve(p, [(-10, 0.2, 0, 0)] * 2)[0]
        self.assertEqual(warm[0], 1)
        self.assertEqual(cold[0], 1)
        self.assertGreater(cold[2], warm[2])

    def test_milp_chooses_cheap_hot_water_charge(self):
        p, _ = scenario(2)
        p[1] = 0  # no building loss in this hand-checkable fixture
        p[3] = 0
        periods = [(0, 0.05, 0, 4), (0, 0.8, 0, 0)]
        schedule, report = solve_milp(p, periods)
        self.assertTrue(report["feasible"])
        self.assertEqual(schedule[0][0], 2)
        self.assertEqual(schedule[1][0], 0)
        self.assertAlmostEqual(report["cost_eur"], 0.05 / 3.025, places=6)
        self.assertTrue(replay(p, periods, schedule)["feasible"])

    def test_price_threshold_preheats_only_in_cheap_idle_period(self):
        p, _ = scenario(1)
        p[1] = 0
        p[3] = 0
        cheap = price_threshold(p, [(0, 0.05, 0, 0)])[0]
        expensive = price_threshold(p, [(0, 0.50, 0, 0)])[0]
        self.assertNotEqual(cheap[0], 0)
        self.assertEqual(expensive[0], 0)


if __name__ == "__main__":
    unittest.main()
