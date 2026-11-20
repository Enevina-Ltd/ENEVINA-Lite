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

## Comfort wizard: crash-safe two-stage journal

When the wizard changes Hold/Occupied target and profile/bounds, the first
HTTP POST writes site/comfort_txn as a durable PREPARED record including
the previous site profile, limits, setup step and previous mode target.
It saves and reads back the new site record, then rewrites the same journal
atomically as SITE_COMMITTED before returning success. The subsequent air
POST persists the mode target and verifies all four air mode NVS keys before
clearing the journal.

On reboot with PREPARED, a verified old air configuration allows the old site
profile/bounds to be restored and checked against NVS; no new target is
replayed. With SITE_COMMITTED and matching site+mode, the desired air target
is completed and verified. A mismatching mode or a failed air save triggers
verified rollback to the previous site band when safe; partial/unreadable NVS
remains blocked, never falsely reported as a successful recovery. Repeating
the same committed site step is idempotent; a different target first rolls
back a verified old in-flight journal. The wizard can retry after a dropped
HTTP response without guessing a half-write was successful.

Hard comfort limits are checked against the selected Hold/Occupied target
in both site and air POST handlers and in the controller before a proposal.
For example a 19 C owner minimum with a 12 C Hold target is rejected rather
than silently dropped. No new passwords, physical pump writes or automatic
restarts are introduced. Site v4 layout remains unchanged; journal version 2.

Host tests cover phase transitions, target-only unchanged-site ambiguity,
hard lower/upper limits, retries, and source ordering. Real power-cut
injection against ESP32-S3 NVS is a separate release criterion.


### OTA probation and migration records

On an OTA first boot, site/air crash recovery can save the site settings
BEFORE the signed OTA service has loaded its transaction. A missing 'pending'
flag is not proof of confirmation: old site v1/v2/v3 (and v4 in subsequent
images) are reclaimable only when the running ESP-IDF image state is
ESP_OTA_IMG_VALID **and** no signed OTA confirmation is pending. Unknown,
pending-verify and failed state reads leave old records in place. Host and
source tests enforce this non-destructive ordering; a real hardware
power-cut and A/B rollback test remains a release criterion.
