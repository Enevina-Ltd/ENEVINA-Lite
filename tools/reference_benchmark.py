"""Reproducible PC-only MILP comparison. Requires SciPy >= 1.9.

The MILP is formulated independently of the C++ dynamic program. The Python
simulator replays every chosen action under the same stated equations and
rejects any comfort or terminal violation.
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import tempfile
import time
from pathlib import Path

import numpy as np
from scipy.optimize import Bounds, LinearConstraint, milp
from scipy.sparse import lil_matrix

DT = 0.25


def choices(p, outdoor):
    capacity = max(0.0, p[4] - p[5] * max(0.0, -outdoor))
    return [(0, 0.0, p[12]), (1, capacity / 2, p[12]),
            (1, capacity, p[12]), (1, capacity / 2, p[13]),
            (1, capacity, p[13]), (2, capacity / 2, p[14]),
            (2, capacity, p[14])]


def action_effect(p, period, action):
    mode, requested, supply = action
    outdoor, price, _, _ = period
    delivered = min(max(0.0, requested), max(0.0, p[4] - p[5] * max(0.0, -outdoor)))
    if mode == 1:
        delivered = min(delivered, p[11] * max(0.0, supply - (p[15] + p[16]) / 2))
    elif mode == 0:
        delivered = 0.0
    cop = min(p[10], max(p[9], p[6] + p[7] * outdoor - p[8] * (supply - 35)))
    return (delivered if mode == 1 else 0.0,
            delivered if mode == 2 else 0.0,
            delivered * DT / cop,
            delivered * DT / cop * price)


def replay(p, periods, schedule):
    indoor, tank = p[18], p[19]
    total_cost = total_electric = total_heat = 0.0
    min_indoor = indoor
    min_tank = tank
    valid = len(periods) == len(schedule)
    for period, action in zip(periods, schedule):
        outdoor, _, gain, draw = period
        space, dhw, electric, cost = action_effect(p, period, action)
        indoor += DT * (space + gain - p[1] * (indoor - outdoor)) / p[0]
        tank += DT * (dhw - draw - p[3] * tank)
        total_cost += cost
        total_electric += electric
        total_heat += DT * (space + dhw)
        min_indoor = min(min_indoor, indoor)
        min_tank = min(min_tank, tank)
        valid &= p[15] - 1e-7 <= indoor <= p[16] + 1e-7
        valid &= p[17] - 1e-7 <= tank <= p[2] + 1e-7
    valid &= indoor >= p[18] - 1e-7 and tank >= p[19] - 1e-7
    return dict(feasible=bool(valid), cost_eur=total_cost, electric_kwh=total_electric,
                thermal_kwh=total_heat, mean_cop=total_heat / total_electric if total_electric else None,
                minimum_indoor_c=min_indoor, minimum_tank_kwh=min_tank,
                final_indoor_c=indoor, final_tank_kwh=tank)


def solve_milp(p, periods, time_limit=60):
    n = len(periods)
    action_count = 7 * n
    variables = action_count + 2 * (n + 1)
    temp = lambda t: action_count + t
    tank = lambda t: action_count + n + 1 + t
    objective = np.zeros(variables)
    lower = np.zeros(variables)
    upper = np.ones(variables)
    lower[action_count:action_count+n+1] = p[15]
    upper[action_count:action_count+n+1] = p[16]
    lower[action_count+n+1:] = p[17]
    upper[action_count+n+1:] = p[2]
    lower[temp(0)] = upper[temp(0)] = p[18]
    lower[tank(0)] = upper[tank(0)] = p[19]
    lower[temp(n)] = max(lower[temp(n)], p[18])
    lower[tank(n)] = max(lower[tank(n)], p[19])
    integrality = np.zeros(variables)
    integrality[:action_count] = 1
    matrix = lil_matrix((3 * n, variables))
    rhs = np.zeros(3 * n)
    for t, period in enumerate(periods):
        outdoor, _, gain, draw = period
        row = 3 * t
        matrix[row, 7*t:7*t+7] = 1
        rhs[row] = 1
        matrix[row+1, temp(t+1)] = 1
        matrix[row+1, temp(t)] = -(1 - DT * p[1] / p[0])
        rhs[row+1] = DT * (gain + p[1] * outdoor) / p[0]
        matrix[row+2, tank(t+1)] = 1
        matrix[row+2, tank(t)] = -(1 - DT * p[3])
        rhs[row+2] = -DT * draw
        for a, choice in enumerate(choices(p, outdoor)):
            space, dhw, _, cost = action_effect(p, period, choice)
            objective[7*t+a] = cost
            matrix[row+1, 7*t+a] = -DT * space / p[0]
            matrix[row+2, 7*t+a] = -DT * dhw
    started = time.perf_counter()
    result = milp(objective, integrality=integrality,
                  bounds=Bounds(lower, upper),
                  constraints=LinearConstraint(matrix.tocsr(), rhs, rhs),
                  options={"time_limit": time_limit, "mip_rel_gap": 0.001})
    seconds = time.perf_counter() - started
    if result.x is None:
        return None, dict(status=int(result.status), message=result.message, seconds=seconds)
    schedule = [choices(p, periods[t][0])[int(np.argmax(result.x[7*t:7*t+7]))]
                for t in range(n)]
    report = replay(p, periods, schedule)
    report.update(seconds=seconds, solver_status=int(result.status),
                  solver_objective_eur=float(result.fun),
                  solver_gap=float(result.mip_gap) if result.mip_gap is not None else None)
    return schedule, report


def heating_curve(p, periods):
    indoor, tank = p[18], p[19]
    schedule = []
    for period in periods:
        outdoor, _, gain, draw = period
        options = choices(p, outdoor)
        needs_room = indoor + DT * (gain - p[1] * (indoor - outdoor)) / p[0] < p[18]
        needs_tank = tank - DT * (draw + p[3] * tank) < p[19]
        if needs_tank and (tank - p[17] < 1 or not needs_room):
            action = options[6]
        elif needs_room:
            supply = min(p[13], max(p[12], p[12] + 0.8 * (5 - outdoor)))
            action = (1, max(0.0, p[4] - p[5] * max(0.0, -outdoor)), supply)
        elif needs_tank:
            action = options[5]
        else:
            action = options[0]
        schedule.append(action)
        space, dhw, _, _ = action_effect(p, period, action)
        indoor += DT * (space + gain - p[1] * (indoor - outdoor)) / p[0]
        tank += DT * (dhw - draw - p[3] * tank)
    return schedule


def price_threshold(p, periods):
    """Fixed 0.15 EUR/kWh preheat threshold with comfort emergency override."""
    indoor, tank = p[18], p[19]
    schedule = []
    for period in periods:
        outdoor, price, gain, draw = period
        options = choices(p, outdoor)
        idle_room = indoor + DT * (gain - p[1] * (indoor - outdoor)) / p[0]
        idle_tank = tank - DT * (draw + p[3] * tank)
        emergency_tank = idle_tank < p[19]
        emergency_room = idle_room < p[18]
        if emergency_tank and (tank - p[17] < 1 or not emergency_room):
            action = options[6]
        elif emergency_room:
            supply = min(p[13], max(p[12], p[12] + 0.8 * (5 - outdoor)))
            action = (1, options[2][1], supply)
        elif emergency_tank:
            action = options[5]
        elif price <= 0.15 and tank < p[19] + 1:
            action = options[5]
        elif price <= 0.15 and indoor < p[18] + 0.3:
            supply = min(p[13], max(p[12], p[12] + 0.8 * (5 - outdoor)))
            action = (1, options[2][1], supply)
        else:
            action = options[0]
        schedule.append(action)
        space, dhw, _, _ = action_effect(p, period, action)
        indoor += DT * (space + gain - p[1] * (indoor - outdoor)) / p[0]
        tank += DT * (dhw - draw - p[3] * tank)
    return schedule


def cpp_optimizer(executable, p, periods):
    with tempfile.TemporaryDirectory(dir=Path.cwd() / "build") as directory:
        path = Path(directory) / "scenario.csv"
        path.write_text(",".join(map(str, p)) + "\n" +
                        "\n".join(",".join(map(str, row)) for row in periods) + "\n")
        run = subprocess.run([str(executable), str(path)], capture_output=True, text=True)
    if run.returncode:
        return None, dict(status="failed", detail=run.stderr.strip())
    lines = run.stdout.splitlines()
    expected_cost = float(lines[0].split(",")[1])
    seconds = float(lines[1].split(",")[1])
    peak_rss_bytes = int(lines[2].split(",")[1])
    schedule = [tuple(map(float, line.split(","))) for line in lines[3:]]
    report = replay(p, periods, schedule)
    report["seconds"] = seconds
    report["peak_rss_bytes"] = peak_rss_bytes
    report["cpp_cost_matches_replay"] = math.isclose(expected_cost, report["cost_eur"], abs_tol=1e-7)
    return schedule, report


def scenario(n, cold=False):
    p = [12.0 if not cold else 8.0, 0.18 if not cold else 0.24,
         8.0, 0.002, 8.0, 0.1, 3.4, 0.055, 0.025,
         1.2, 5.0, 0.8, 35.0, 45.0, 50.0,
         19.0, 21.0, 2.0, 20.0, 5.0]
    periods = []
    for t in range(n):
        outdoor = (-4 if cold else 2) + 2 * math.sin(2 * math.pi * t / 96)
        price = (0.08 if t % 48 < 16 else 0.42) + (0.02 if t % 12 < 4 else 0)
        gain = 0.2 if 28 <= t % 96 < 68 else 0.0
        draw = 2.0 if t % 24 in (7, 8, 20) else 0.0
        periods.append((outdoor, price, gain, draw))
    return p, periods


def radiator_scenario(n):
    """A lower-output pump, emitter-limited supply and volatile total tariff."""
    p, _ = scenario(n)
    p[0] = 10.0
    p[1] = 0.21
    p[4] = 7.0
    p[5] = 0.07
    p[6] = 3.0
    p[7] = 0.07
    p[8] = 0.035
    p[11] = 0.36
    p[13] = 50.0
    p[14] = 52.0
    periods = []
    prices = [0.12] * 8 + [0.50] * 8 + [0.06] * 8 + [0.28] * 8
    for t in range(n):
        outdoor = -2 + 4 * math.sin(2 * math.pi * t / 96)
        price = prices[t % len(prices)]
        gain = 0.15 if 28 <= t % 96 < 68 else 0.0
        draw = 2.0 if t % 24 in (7, 8, 20) else 0.0
        periods.append((outdoor, price, gain, draw))
    return p, periods


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--horizons", type=int, nargs="+", default=[24, 48, 96, 192])
    parser.add_argument("--cases", nargs="+", choices=["mild", "cold", "radiator"],
                        default=["mild", "cold", "radiator"])
    args = parser.parse_args()
    results = []
    cases = [("mild", lambda n: scenario(n, False)),
             ("cold", lambda n: scenario(n, True)),
             ("radiator", radiator_scenario)]
    for name, make in cases:
        if name not in args.cases:
            continue
        for n in args.horizons:
            p, periods = make(n)
            _, lite = cpp_optimizer(args.executable, p, periods)
            _, reference = solve_milp(p, periods)
            curve = replay(p, periods, heating_curve(p, periods))
            threshold = replay(p, periods, price_threshold(p, periods))
            entry = dict(case=name, periods=n,
                         lite=lite, reference_milp=reference,
                         heating_curve=curve, price_threshold=threshold)
            if lite.get("feasible") and reference.get("feasible"):
                entry["lite_gap_percent"] = 100 * (lite["cost_eur"] /
                    reference["cost_eur"] - 1) if reference["cost_eur"] else 0
            results.append(entry)
            print(f"{entry['case']} {n}: Lite={lite.get('cost_eur')} "
                  f"MILP={reference.get('cost_eur')} curve={curve['cost_eur']}")
    text = json.dumps(results, indent=2)
    if args.output:
        args.output.write_text(text + "\n")
    else:
        print(text)


if __name__ == "__main__":
    main()
