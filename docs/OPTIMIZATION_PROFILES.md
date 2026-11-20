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
0.06 EUR/kWh (0.70 SEK/kWh). Reproduce: rebuild and run `profile_benchmark tests/data/prices`.

The previous unadjusted cost-percentage table has been withdrawn: its
runs finished at different indoor temperatures, effectively crediting
unbilled depletion of stored building heat. The results below instead add
an **end-of-horizon thermal-energy equivalent** to equalize their terminal
building energy. This is **an accounting adjustment, not a physically
simulated recovery schedule**. The repayment uses an idealized terminal COP
and the last four hours' recorded average electricity price. Neither
recovery timing nor the actual achievable saving is proven.

| Zone (7 days) | Profile | Raw cost | End C | Heat debt kWh | Equalized cost | vs equalized thermostat |
|---|---|---:|---:|---:|---:|---:|
| Finland FI | thermostat (no prices) | 23.34 EUR | 21.42 | 0.00 | 23.34 EUR | +0.0 % |
| Finland FI | comfort | 23.19 EUR | 21.19 | 1.38 | 23.27 EUR | -0.3 % |
| Finland FI | balanced | 22.31 EUR | 20.69 | 4.40 | 22.58 EUR | -3.3 % |
| Finland FI | saving | 21.66 EUR | 20.00 | 8.50 | 22.18 EUR | -5.0 % |
| Denmark DK1 | thermostat (no prices) | 41.71 EUR | 21.42 | 0.00 | 41.71 EUR | +0.0 % |
| Denmark DK1 | comfort | 41.10 EUR | 21.31 | 0.64 | 41.16 EUR | -1.3 % |
| Denmark DK1 | balanced | 40.40 EUR | 20.90 | 3.12 | 40.71 EUR | -2.4 % |
| Denmark DK1 | saving | 39.24 EUR | 20.48 | 5.67 | 39.79 EUR | -4.6 % |
| Sweden SE3 | thermostat (no prices) | 378.34 SEK | 21.42 | 0.00 | 378.34 SEK | +0.0 % |
| Sweden SE3 | comfort | 376.10 SEK | 21.35 | 0.42 | 376.49 SEK | -0.5 % |
| Sweden SE3 | balanced | 363.83 SEK | 20.48 | 5.62 | 369.02 SEK | -2.5 % |
| Sweden SE3 | saving | 354.08 SEK | 19.97 | 8.72 | 362.14 SEK | -4.3 % |

Reading it honestly:

* These results are **synthetic and not measured savings**. Even the
  normalized ratios depend on assumed terminal COP, repayment prices, pump
  capacity and thermal mass. Do not present them as verified savings.
* More aggressive profiles can run colder and store less heat. The updated
  live controller **stops pre-heating at its permitted ceiling** and
  **resumes heating when the profile-specific coasting allowance ends**.
  A HOLD at those limits previously allowed a running or stopped pump to
  continue in the wrong direction.
* The simulator does not model the Supervisor's actual minimum run/stop
  timings or prove that a heat pump can restore the terminal energy debt
  within comfort limits. A real undersized heat pump may still undershoot
  the selected comfort limit: field validation and calibrated physics
  remain necessary.

## Not done

* Profiles are not applied to the shadow MPC; MILP/MPC comparison per profile is open.
* The "continuously inhabited vs cottage" question maps to the existing modes
  (Ylläpitolämpö / Asuttu) and is not asked separately.
* The wizard never arms automatic control; there is no "use automatic optimization"
  toggle by design.
* No field data; no thermal-model calibration.

## Comfort wizard: power-loss recovery for site and air NVS (v4)

When Comfort changes optimization limits and the Hold/Occupied target, the first
site POST writes one durable, versioned site/comfort_txn intent before writing
the new site profile and bounds. /api/air/config removes the intent only
after verifying the target and site data. The firmware retains the existing
v4 settings layout, with no new passwords or pump commands.

After a reset before site commit, the old site and target remain authoritative
and the intent is discarded. After site commit but before air commit,
c23_initialize resumes the target write and verifies all four mode NVS keys
before removing the marker. The case after both commits is idempotent.
Unreadable journals, mismatched modes and failed NVS recovery fail closed
to block physical commands rather than using inconsistent settings.
Host tests cover transaction states, wizard parameters and firmware ordering.
Physical NVS reset/fault injection remains a separate release criterion.
