# C0: Mitsubishi FD25VA cottage shadow optimizer (2026-11-16)

**Status: PC-only C0 research prototype. No physical device has been controlled.
No firmware was flashed. This is NOT a freeze-protection guarantee or release approval.**

## Scope and reuse

This branch is stacked on PR #1 `feat/g0-physics-optimizer`; shared
C++17, 15-minute UTC forecast, pricing, and FMI components are preserved.
The new `cottage/` code is independent of the hydronic space/DHW physics.

- `CottagePhysics` uses a one-zone exact-discretized RC heat balance.
  Heater power and COP are *site-supplied, test-only* linear reference models,
  explicitly **not** Mitsubishi performance curves. A defrost factor reduces
  effective thermal capacity; compressor startup adds a delay and electric cost.
  Actual HEAT-16 thermostat cycling is not yet calibrated. House electrical
  loads may be included as internal thermal gains only if separately measured.
- `CottageMPC`: 96–192 15-minute periods, deterministic bounded-grid search,
  maximum 3 MiB parent/history budget, minimum HEAT/OFF mode times,
  forecast indoor minimum and terminal temperature constraints, switching
  penalties, exact continuous-state replay before accepting a plan.
- `CottageSupervisor` and typed observation adapters are permanently
  read-only. They simulate HEAT-16/OFF and report fault/fallback reasons.
  They do not initiate network requests or issue any device commands.

The **8 °C** threshold is a *model planning floor*, 5 °C is the critical
alarm concept. Neither is an acceptable setpoint sent to the pump; the
only hypothetical heat setpoint is **16 °C**. Even a valid MPC schedule cannot
guarantee avoiding freezing. Use independently verified physical protection.

## PC test results

Compiler: GNU C++ 15.2.0, C++17, `-O2 -Wall -Wextra -Wpedantic`,
isolated working files on a Linux test host. No production services modified.

| Scenario, horizon | Cottage MPC electric cost € | Independent SciPy/HiGHS MILP € | MPC extra cost | MILP status |
|---|---:|---:|---:|---|
| Mild, 96 slots (24 h) | 3.051305 | 3.050351 | +0.031% | status 0 |
| Cold, 96 slots (24 h) | 6.138770 | 6.038643 | +1.658% | status 0 |
| Price spike, 96 slots (24 h) | 5.090182 | 5.089550 | +0.012% | status 0 |
| Synthetic defrost, 96 slots (24 h) | 3.161049 | 3.133774 | +0.870% | status 0 |

All four MILP runs used a nominal 0.1% relative MIP gap, the same
initial/terminal temperature, thermal model, ambient temperatures, prices,
command-state constraints and planning lower/upper bounds. Results are
**synthetic model costs**, not actual electricity bills or estimated savings.
The MILP is linear in this test domain (room at most 14.5 °C, below
the simulated 16 °C thermostat cycling region). A 192-slot MILP
comparison has **not yet completed within the available compute budget**.
C++ 192-slot PC cases did complete, but must not be reported as ESP32 results.

Standalone C++ unit checks (physics, 96/192 period search, limiting
temperature, thermal recovery, startup dwell, missing/NaN prices,
small memory budget, infeasible hard frost) and supervisor/adapters
(aging measurements, fault, watchdog, freeze recommendation, fallback,
simulated 16 °C and OFF, bad Shelly data) passed.

The synthetic example (thermal mass 2 kWh/K, loss 0.12 kW/K,
3.2 kW at 0 °C, COP 2.8 at 0 °C) is **not** calibrated to this cottage.
Its HEAT-16 approximation can overestimate heat supply. Hard frost can
make the optimization infeasible. Exposing a false economic saving would
be misleading.

## Reproduction

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python tools/cottage_reference.py --exe ./build/cottage_cli --short \
  --output docs/benchmarks/cottage-c0-local.json
```

MILP reference needs Python with NumPy and SciPy/HiGHS. For 192 slots,
increase the MILP time limit only on a dedicated research environment
and inspect solver status and gap. Neither pricing nor meteorological
transport integrations are claimed tested in this branch.

## C1/C2/C3 gates

- **C1 partially verified:** Current CN105 `/events` SSE HTTP 200 and Shelly Plus 1PM read-only RPC HTTP 200 confirmed on 2026-11-16 (see [C1 evidence](C1-cottage-probe.md)). Continue to inspect
  `/events` SSE payload and timestamps; verify packet semantics and pin
  electrical levels; observe real indoor temperature and compressor Hz.
  Verify Shelly Plus 1PM read-only Switch.GetStatus telemetry and separate
  power/energy from unrelated cottage loads. Establish independent
  temperature sensor location, plausible readings, clock/time zone.
- **C2 unresolved:** build and test an ESP32-S3 **Wi-Fi** shadow firmware
  with the actual observed CN105 payload, price/FMI ingestion, trusted
  UTC time and retained caches, a local UI and a nonblocking watchdog.
  Bench with concurrent Wi-Fi, SSE, HTTP UI and optimization load;
  collect actual runtime, peak RAM/PSRAM, fault logs and long soak data.
- **C3 forbidden until separate approval:** independent verified freeze
  protection, model and compressor validation, correct HEAT-16/OFF ACKs,
  manual recovery, safe command limits and tested failover; existing CN105
  controller must not be reflashed or changed automatically.

Read-only typed observers cannot prove a live connection, or a verified
pump command acknowledgement. Shelly relay switching is not implemented
and must never be added to this integration. Other Enevina products
are out of scope.
