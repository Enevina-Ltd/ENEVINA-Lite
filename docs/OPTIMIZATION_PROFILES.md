# Optimization profiles (19.11.2026)

**Status: implemented in the live price-aware room controller, host-tested, and
compared in SIMULATION on real recorded prices. Not measured on a building, not
run on a flashed board.** A profile never authorizes a physical command; arming
automatic control stays a separate owner action and `PumpAdapter`/`Supervisor`
still gate every write.

## What a profile changes (and what it does not)

The controller that actually decides heating today is `cottage/price_bootstrap.hpp`
(model-free). The MPC in `cottage/mpc.cpp` stays shadow-only because there is no
calibrated building model yet and its HEAT16/OFF physics is validated only up to
16 C - see `OPTIMIZATION_AUDIT_20261118.md`. Profiles therefore tune the live
path, not a fictional optimizer.

The owner sets a target (existing mode configuration) and optional limits
`comfort_min_c` / `comfort_max_c` (5-26 C, 0.5 steps, >= 1 C apart; target must be
at least 0.5 C inside them). A profile only decides how much of that band and of
the price signal may be used:

| | Comfort | Balanced | Saving |
|---|---|---|---|
| Price threshold (x owner base, default 10 %) | 2x | 1x | 0.5x |
| Share of band used for cheap pre-heat / expensive coasting | 0 % | 40 % | 80 % |
| Acts inside the owner's limits by | 0 | 0.25 C | 0.75 C |
| Price window compared to the median | 2 h | 2 h | 4 h (>= 2 h if less is published) |

* **Limits are decision rules, not just targets:** at or below the lower working
  limit the controller heats, at or above the upper it switches off, for every
  profile, price and temperature (property-tested over ~1,400 combinations).
* **No band set = no behaviour change.** Balanced without limits is byte-for-byte
  the pre-profile decision (tested over several hundred state/price combinations),
  so upgraded devices behave as before until the owner chooses otherwise.
* Without usable prices (outage) every profile is a plain thermostat on target +-0.5 C.
* Invalid or contradictory settings produce *no decision*, never a guessed one.

## Simulated comparison on real prices

Setup: 7 days (2026-11-12..18) of real 15-minute Nord Pool day-ahead prices for
three zones (`tests/data/prices`); **synthetic** building (6 kWh/K, 0.12 kW/K),
**synthetic** weather (-3 C mean) and an idealized thermostat heat pump using the
generic cottage capacity/COP curves; target 21 C, limits 19..23 C; example add-on
0.06 EUR/kWh (0.70 SEK/kWh). Reproduce: `profile_benchmark tests/data/prices`.

| Zone (7 days, 15-min real prices) | Profile | Cost | kWh | Cost vs thermostat | Heating share | Mode switches | Room min..max C |
|---|---|---:|---:|---:|---:|---:|---|
| Finland FI | thermostat (no prices) | 23.34 EUR | 178.7 | 0 % | 93 % | 9 | 20.28..21.50 |
| Finland FI | comfort | 23.11 EUR | 176.9 | -1.0 % | 92 % | 12 | 20.09..21.51 |
| Finland FI | balanced | 22.03 EUR | 173.8 | -5.6 % | 90 % | 26 | 19.53..21.53 |
| Finland FI | saving | 21.48 EUR | 170.3 | -8.0 % | 88 % | 32 | 19.33..21.50 |
| Denmark DK1 | thermostat (no prices) | 41.71 EUR | 178.7 | 0 % | 93 % | 9 | 20.28..21.50 |
| Denmark DK1 | comfort | 41.38 EUR | 177.5 | -0.8 % | 92 % | 11 | 19.98..21.50 |
| Denmark DK1 | balanced | 40.20 EUR | 173.4 | -3.6 % | 90 % | 23 | 19.50..21.51 |
| Denmark DK1 | saving | 38.92 EUR | 169.7 | -6.7 % | 88 % | 29 | 19.20..21.00 |
| Sweden SE3 | thermostat (no prices) | 378.34 SEK | 178.7 | 0 % | 93 % | 9 | 20.28..21.50 |
| Sweden SE3 | comfort | 374.83 SEK | 176.9 | -0.9 % | 92 % | 11 | 19.97..21.50 |
| Sweden SE3 | balanced | 363.62 SEK | 174.1 | -3.9 % | 90 % | 26 | 19.62..21.53 |
| Sweden SE3 | saving | 349.54 SEK | 168.0 | -7.6 % | 87 % | 41 | 19.21..21.18 |

Reading it honestly:

* These are **simulated** cost ratios under the assumptions above, not measured
  savings, and they depend on the price shape of that week and on the synthetic
  building. Do not quote them to customers.
* More aggressive profiles shift heating away from expensive slots, use less energy
  in this setup, mainly because the room runs cooler on average, and switch modes
  more often (more pump starts: the Supervisor's minimum run/stop times are not
  modelled in this simulation).
* **A finding that shaped the design:** with limits acted on exactly at the limit,
  Saving let the simulated room reach 18.65 C against a 19 C limit - after coasting,
  a capacity-limited pump could not recover in a cold spell. The per-profile
  safety margin (0.25 / 0.75 C) fixes this in the simulated weeks (min 19.2 C or
  above for all zones), but the controller has no knowledge of pump capacity, so
  **a real undersized pump can still undershoot**; calibrated capacity (or the MPC)
  is needed for a guarantee.

## Not done

* Profiles are not applied to the shadow MPC; MILP/MPC comparison per profile is open.
* The "continuously inhabited vs cottage" question maps to the existing modes
  (Ylläpitolämpö / Asuttu) and is not asked separately.
* The wizard never arms automatic control; there is no "use automatic optimization"
  toggle by design.
* No field data; no thermal-model calibration.
