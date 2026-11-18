"""Regression for the G0 optimizer stored-heat tie-break (audit 18.11.2026).

The fixed 0.05 EUR/kWh stored-heat value made the hydronic optimizer lose
4.78-4.81 % to the independent MILP in the radiator case. The data-driven
value (mean remaining price/COP) reduced it to 2.42 % / 1.25 %. MILP costs
below come from tools/reference_benchmark.py (SciPy 1.18.1 HiGHS, same
7 discrete actions, same terminal state); they are constants so this test
needs no solver. Skipped when the Release CLI has not been built.
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import reference_benchmark as rb  # noqa: E402

CLI = ROOT / "build-release" / "enevina_cli"
MILP = {48: 2.5871538780792163, 96: 6.821664875618126, 192: 13.340901882589902}


@unittest.skipUnless(CLI.exists(), "build-release/enevina_cli not built")
class StoredValueRegression(unittest.TestCase):
    def test_radiator_gap_stays_below_three_percent(self):
        (ROOT / "build").mkdir(exist_ok=True)  # cpp_optimizer uses ./build
        for n, reference in MILP.items():
            p, periods = rb.radiator_scenario(n)
            _, lite = rb.cpp_optimizer(CLI, p, periods)
            self.assertTrue(lite["feasible"], lite)
            self.assertTrue(lite["cpp_cost_matches_replay"], lite)
            gap = 100 * (lite["cost_eur"] / reference - 1)
            self.assertLess(gap, 3.0, f"{n} slots: gap {gap:.2f} %")


if __name__ == "__main__":
    unittest.main()
