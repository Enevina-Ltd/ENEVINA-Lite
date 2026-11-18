#!/usr/bin/env python3
"""Independent optimization audit for the Mitsubishi HEAT16/OFF cottage model.

Runs every scenario x horizon through FOUR controllers on the SAME physics,
total-price slots, initial state, dwell limits and comfort limits:

  1. thermostat       ordinary hysteresis thermostat (cottage_audit_cli)
  2. price            existing firmware price bootstrap, PR #22 (cottage_audit_cli)
  3. mpc              Enevina Lite cottage MPC, cottage/mpc.cpp (cottage_audit_cli)
  4. milp             independent SciPy/HiGHS MILP, tools/cottage_reference.py

Every schedule is replayed by the independent Python model (cottage_reference
.replay); a C++ cost is never trusted on its own. Results are SYNTHETIC: the
parameters are not a calibrated building and not a Mitsubishi specification.

Terminal fairness: MPC and MILP must end >= the initial temperature. The two
rule baselines get the same end-of-horizon obligation; any remaining deficit is
reported and its reheating cost is added in `equalized_cost_eur` (deficit *
thermal mass / mean COP * mean total price). Savings % use equalized costs.

usage:
  python3 tools/cottage_optimization_audit.py build-release/cottage_audit_cli \
      --output docs/benchmarks/cottage-optimization-audit.json \
      --markdown docs/benchmarks/cottage-optimization-audit.md
"""
import argparse
import calendar
import json
import math
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cottage_reference import replay, solve  # noqa: E402

BASE = dict(mass=2.0, loss=0.12, heat_at_zero=3.2, heat_slope=0.03,
            min_heat_kw=1.0, cop_at_zero=2.8, cop_slope=0.04, min_cop=1.2,
            max_cop=5.0, heat_standby_kw=0.012, off_standby_kw=0.002,
            startup_kwh=0.05, startup_delay_min=3.0, minimum_c=8.0,
            critical_c=5.0, uncertainty_c=0.0, maximum_c=14.5,
            switch_penalty=0.03, min_heat_min=60, min_off_min=60)
# Example Finnish structure, EUR/kWh VAT 0 except where flagged. NOT a contract.
TARIFF = dict(margin=0.0049, transfer_day=0.0345, transfer_night=0.0210,
              tax=0.02827515, vat=0.255, tax_includes_vat=1)
WINTER_START = calendar.timegm((2026, 1, 11, 22, 0, 0))   # 00:00 EET
AUTUMN_DST = calendar.timegm((2026, 10, 24, 21, 0, 0))    # 00:00 EEST, DST ends 25.10.
SPRING_DST = calendar.timegm((2026, 3, 28, 22, 0, 0))     # 00:00 EET, DST starts 29.3.


def local_hour(i, start):
    # Only used to SHAPE the synthetic series. Tariff day/night is decided in
    # C++ by finnish_day_transfer() from the real UTC timestamp.
    return ((i // 4) % 24)


def daily_spot(h):
    """Ordinary winter day-ahead shape: morning and evening peaks."""
    return 0.045 + 0.06 * math.exp(-((h - 8) / 2.0) ** 2) + \
        0.09 * math.exp(-((h - 18) / 2.5) ** 2)


def outdoor_sine(i, mean, amp=2.0):
    return mean + amp * math.sin(2 * math.pi * (i / 4 - 9) / 24)


def make(case, n):
    p = dict(BASE)
    start = WINTER_START
    initial = (10.0, 0, 180)
    target = 10.0
    forecast_bias = 0.0
    slots = []
    for i in range(n):
        h = local_hour(i, start)
        spot = daily_spot(h)
        out = outdoor_sine(i, -5.0)
        defrost = 1.0
        if case == "flat_price":
            spot = 0.08
        elif case == "price_spikes":
            spot = 0.05 + (1.20 if h in (7, 8, 17, 18, 19) else 0.0)
        elif case == "negative_prices":
            spot = -0.06 if 10 <= h < 15 else 0.06
        elif case == "expensive_night_cheap_day":
            spot = 0.28 if (h >= 22 or h < 7) else 0.03
        elif case == "cheap_night_expensive_day":
            spot = 0.01 if (h >= 22 or h < 7) else 0.26
        elif case == "hard_frost":
            p.update(loss=0.09, heat_at_zero=4.5, heat_slope=0.04)
            out = outdoor_sine(i, -22.0)
        elif case == "mild":
            out = outdoor_sine(i, 6.0)
        elif case == "fast_outdoor_drop":
            out = 2.0 if i < 48 else max(-18.0, 2.0 - (i - 48) * 20 / 24)
        elif case == "slow_building":
            p.update(mass=6.0, loss=0.10)
        elif case == "fast_cooling_building":
            p.update(mass=0.8, loss=0.20, heat_at_zero=4.0)
            out = outdoor_sine(i, -3.0)
        elif case == "oversized_pump":
            p.update(heat_at_zero=6.5, heat_slope=0.06, min_heat_kw=2.0)
        elif case == "undersized_pump":
            p.update(heat_at_zero=2.3, heat_slope=0.03, min_heat_kw=0.6)
            out = outdoor_sine(i, -4.0, 1.0)
        elif case == "poor_cold_cop":
            p.update(cop_at_zero=2.2, cop_slope=0.07, min_cop=1.0,
                     loss=0.10, heat_at_zero=4.2)
            out = outdoor_sine(i, -15.0, 4.0)
        elif case == "defrost_derating":
            defrost = 0.65 if i % 32 >= 12 and i % 32 < 17 else 1.0
        elif case == "dst_autumn":
            start = AUTUMN_DST
        elif case == "dst_spring":
            start = SPRING_DST
        elif case in ("stale_weather_forecast", "stale_weather_forecast_margin1"):
            forecast_bias = +4.0   # forecast 4 C too warm: under-heating risk
            if case.endswith("margin1"):
                p.update(uncertainty_c=1.0)  # mitigation: 1 C planning margin
        elif case != "base":
            raise ValueError(case)
        slots.append([start + 900 * i, spot, out + forecast_bias, out, 0.0, defrost])
    return p, initial, target, slots


CASES = ["base", "flat_price", "price_spikes", "negative_prices",
         "expensive_night_cheap_day", "cheap_night_expensive_day", "hard_frost",
         "mild", "fast_outdoor_drop", "slow_building", "fast_cooling_building",
         "oversized_pump", "undersized_pump", "poor_cold_cop", "defrost_derating",
         "dst_autumn", "dst_spring", "stale_weather_forecast",
         "stale_weather_forecast_margin1"]
# Listed for traceability; covered by tests/cottage_optimization_audit_tests.cpp
# (fault paths) or not applicable to an air-to-air HEAT16/OFF pump.
NOT_IN_TABLE = {
    "supply_water_cop_penalty": "NOT_APPLICABLE: air-to-air pump has no supply water; "
                                "covered by hydronic G0 (tools/reference_benchmark.py)",
    "missing_price_slot": "fault path: BridgeStatus::Gap -> no MPC plan (C++ test)",
    "sensor_outage": "fault path: bootstrap UNAVAILABLE, MPC not run (C++ test)",
    "pump_link_outage": "fault path: control gate INPUTS_UNVERIFIED (C++ test)",
    "optimizer_timeout": "fault path: MpcStatus::Timeout, count=0, no partial plan (C++ test)",
    "no_feasible_plan": "fault path: UNSAFE_START / INFEASIBLE, no plan (C++ test)",
}


def write_scenario(path, p, initial, target, slots):
    order = ["mass", "loss", "heat_at_zero", "heat_slope", "min_heat_kw",
             "cop_at_zero", "cop_slope", "min_cop", "max_cop", "heat_standby_kw",
             "off_standby_kw", "startup_kwh", "startup_delay_min", "minimum_c",
             "critical_c", "uncertainty_c", "maximum_c", "switch_penalty",
             "min_heat_min", "min_off_min"]
    t = TARIFF
    lines = ["params " + " ".join(repr(float(p[k])) if k not in
             ("min_heat_min", "min_off_min") else str(int(p[k])) for k in order),
             f"tariff {t['margin']} {t['transfer_day']} {t['transfer_night']} "
             f"{t['tax']} {t['vat']} {t['tax_includes_vat']}",
             f"initial {initial[0]} {initial[1]} {initial[2]} {target}"]
    lines += ["slot %d %.10f %.10f %.10f %.10f %.10f" % tuple(s) for s in slots]
    path.write_text("\n".join(lines) + "\n")


def run_cpp(exe, path, policy):
    out = subprocess.run([str(exe), str(path), policy], check=True,
                         capture_output=True, text=True, timeout=600)
    return json.loads(out.stdout)


def replay_dict(p, initial, slots, totals):
    d = dict(p)
    d.update(initial_c=initial[0], initial_on=bool(initial[1]),
             initial_minutes=initial[2], periods=len(slots),
             slots=[[s[3], tot, s[4], s[5]] for s, tot in zip(slots, totals)])
    return d


def switches(d, schedule):
    on, n = bool(d["initial_on"]), 0
    for a in schedule:
        n += bool(a) != on
        on = bool(a)
    return n


def equalize(d, result):
    deficit = max(0.0, d["initial_c"] - result["final_c"])
    price = sum(s[1] for s in d["slots"]) / len(d["slots"])
    out = sum(s[0] for s in d["slots"]) / len(d["slots"])
    cop = max(d["min_cop"], min(d["max_cop"], d["cop_at_zero"] + d["cop_slope"] * out))
    add = deficit * d["mass"] / cop * price
    result["terminal_deficit_c"] = deficit
    result["equalized_cost_eur"] = result["cost_eur"] + add
    result["mean_cop"] = (result["thermal_kwh"] / result["electric_kwh"]
                          if result["electric_kwh"] > 1e-9 else None)
    return result


def evaluate(exe, case, n, time_limit, workdir):
    p, initial, target, slots = make(case, n)
    path = Path(workdir) / f"{case}_{n}.txt"
    write_scenario(path, p, initial, target, slots)
    entry = dict(case=case, slots=n)
    totals = None
    policies = ["thermostat", "price",
                "mpc_closed_loop" if case.startswith("stale_weather_forecast") else "mpc"]
    for policy in policies:
        r = run_cpp(exe, path, policy)
        totals = r["total_price"]
        d = replay_dict(p, initial, slots, totals)
        key = "mpc" if policy.startswith("mpc") else policy
        if r["status"] not in ("OK",) and not r["status"].startswith("FALLBACK"):
            entry[key] = dict(status=r["status"], feasible=False,
                              solve_ms_max=r["solve_ms_max"], peak_rss_kb=r["peak_rss_kb"])
            continue
        rep = equalize(d, replay(d, r["schedule"]))
        rep["switches"] = switches(d, r["schedule"])
        rep["objective_eur"] = rep["equalized_cost_eur"] + d["switch_penalty"] * rep["switches"]
        rep.update(status=r["status"], solve_ms_max=r["solve_ms_max"],
                   solve_ms_mean=r["solve_ms_mean"], solves=r["solves"],
                   peak_rss_kb=r["peak_rss_kb"])
        # Terminal deficits are equalized, other violations are real failures.
        rep["limit_violations"] = [v for v in rep["violations"] if v != "terminal"]
        rep["feasible"] = not rep["limit_violations"]
        entry[key] = rep
    d = replay_dict(p, initial, slots, totals)
    m = solve(d, time_limit)
    if m.get("feasible"):
        m = equalize(d, m)
        m["switches"] = switches(d, m.pop("actions"))
        m["objective_eur"] = m["equalized_cost_eur"] + d["switch_penalty"] * m["switches"]
        m["optimal_proven"] = m["status"] == 0 and (m.get("mip_gap") or 0) <= 1e-3
    entry["milp"] = m
    # A baseline that breaks comfort/device limits is not a valid reference:
    # its lower EUR is bought with violations. Never report a "saving" then.
    entry["thermostat_comparable"] = entry["thermostat"].get("feasible", False) or \
        not entry["thermostat"].get("limit_violations")
    entry["price_comparable"] = entry["price"].get("feasible", False) or \
        not entry["price"].get("limit_violations")
    base = entry["thermostat"].get("equalized_cost_eur") \
        if entry["thermostat_comparable"] else None
    mpc = entry["mpc"].get("equalized_cost_eur") if entry["mpc"].get("feasible") else None
    if base and mpc is not None:
        entry["mpc_saving_vs_thermostat_pct"] = 100 * (base - mpc) / base
    pr = entry["price"].get("equalized_cost_eur") if entry["price_comparable"] else None
    if pr and mpc is not None:
        entry["mpc_saving_vs_price_pct"] = 100 * (pr - mpc) / pr
    if case.startswith("stale_weather_forecast") and mpc is not None and m.get("feasible"):
        # Closed-loop MPC on a wrong forecast vs PERFECT-FORESIGHT MILP:
        # value of information, not an optimality gap.
        entry["mpc_loss_vs_perfect_foresight_pct"] = 100 * (mpc / m["equalized_cost_eur"] - 1)
    elif mpc is not None and m.get("feasible") and m["cost_eur"] > 0:
        entry["mpc_gap_vs_milp_pct"] = 100 * (mpc / m["equalized_cost_eur"] - 1)
        entry["mpc_objective_gap_vs_milp_pct"] = 100 * (
            entry["mpc"]["objective_eur"] / m["objective_eur"] - 1)
    return entry


def fmt(x, digits=3):
    return "—" if x is None else f"{x:.{digits}f}"


def markdown(records):
    rows = ["| Testitapaus | Jaksot | Perusohjaus € | Hintaraja € | Enevina MPC € | MILP € "
            "| Säästö vs perus % | Ero MILP € % | Ero MILP tavoite % | MPC käynn. | MPC ms (x86) | Rajoitukset / huom. |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|"]
    for e in records:
        notes = []
        for k in ("thermostat", "price", "mpc"):
            r = e[k]
            if not r.get("feasible", False) and "status" in r and not r.get("limit_violations"):
                notes.append(f"{k}: {r['status']}")
            if r.get("limit_violations"):
                notes.append(f"{k}: {len(r['limit_violations'])} rajarikettä")
            if r.get("terminal_deficit_c", 0) > 1e-6:
                notes.append(f"{k}: loppuvaje {r['terminal_deficit_c']:.2f} °C tasattu")
        m = e["milp"]
        if not m.get("feasible"):
            notes.append(f"MILP: ei ratkaisua (status {m.get('status')})")
        elif not m.get("optimal_proven"):
            notes.append("MILP: aikaraja, ei todistettu optimi")
        if e["mpc"].get("status", "OK").startswith("FALLBACK"):
            notes.append("MPC: " + e["mpc"]["status"])
        rows.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
            e["case"], e["slots"],
            fmt(e["thermostat"].get("equalized_cost_eur")),
            fmt(e["price"].get("equalized_cost_eur")),
            fmt(e["mpc"].get("equalized_cost_eur") if e["mpc"].get("feasible") else None),
            fmt(m.get("equalized_cost_eur") if m.get("feasible") else None),
            fmt(e.get("mpc_saving_vs_thermostat_pct"), 1)
            if e.get("thermostat_comparable") else "NOT_COMPARABLE",
            fmt(e.get("mpc_gap_vs_milp_pct"), 2),
            fmt(e.get("mpc_objective_gap_vs_milp_pct"), 2),
            e["mpc"].get("starts", "—"),
            fmt(e["mpc"].get("solve_ms_max"), 1),
            "; ".join(notes) or "OK"))
    rows.append("")
    rows.append("Ei taulukossa (katso C++-vikatestit):")
    for k, v in NOT_IN_TABLE.items():
        rows.append(f"- `{k}`: {v}")
    return "\n".join(rows) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe", type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--markdown", type=Path)
    ap.add_argument("--horizons", type=int, nargs="+", default=[24, 48, 96, 192])
    ap.add_argument("--cases", nargs="+", default=CASES)
    ap.add_argument("--time-limit", type=int, default=30)
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as work:
        jobs = [(c, n) for c in args.cases for n in args.horizons]
        with ThreadPoolExecutor(args.jobs) as pool:
            records = list(pool.map(
                lambda cn: evaluate(args.exe, cn[0], cn[1], args.time_limit, work), jobs))
    for e in records:
        print(e["case"], e["slots"], "thermo", fmt(e["thermostat"].get("equalized_cost_eur")),
              "price", fmt(e["price"].get("equalized_cost_eur")),
              "mpc", fmt(e["mpc"].get("equalized_cost_eur")),
              "milp", fmt(e["milp"].get("equalized_cost_eur")),
              "gap%", fmt(e.get("mpc_gap_vs_milp_pct"), 2))
    meta = dict(synthetic=True, tariff=TARIFF, base_parameters=BASE,
                not_in_table=NOT_IN_TABLE,
                note="Synthetic HEAT16/OFF cottage model; not a calibrated building "
                     "or Mitsubishi specification. Host timings are x86, not ESP32-S3.")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(dict(meta=meta, records=records), indent=1,
                                          allow_nan=False) + "\n")
    if args.markdown:
        args.markdown.write_text(markdown(records))


if __name__ == "__main__":
    main()
