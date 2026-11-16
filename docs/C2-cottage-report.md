# C2 — Mitsubishi FD25VA read-only Wi-Fi shadow, implementation report (2026-11-16)

**C2 STATUS: BENCH PASSED; LIVE INTEGRATION NOT APPROVED.** The full
ESP32-S3 Wi-Fi shadow firmware cross-compiled, and the **separate serial-only
bench image** was flashed to a physically identified ESP32-S3 (16 MB flash,
8 MB PSRAM) on the Core development host. The bench ran optimizer + Wi-Fi
access point + self-generated HTTP requests, **but did NOT connect to the real
CN105, Shelly, Nord Pool, FMI or an independent cottage sensor**. No physical
actuator write command exists in code.

## What was implemented

- `firmware/cottage/`: ESP32-S3 single-device 2.4 GHz Wi-Fi reader/dashboard.
  It has GET-only CN105 /events SSE streaming with bounded frame parsing,
  reconnection, climate HEAT/OFF + temperature + compressor Hz and freshness.
  Shelly Plus 1PM GET-only `/rpc/Switch.GetStatus?id=0` collects entire
  cottage electric power, cumulative energy and read-only relay state.
  BLE Ruuvi RAWv5 reader requires an explicitly configured sensor MAC.
  Missing independent sensor disables all economic plans. No "fake sensor"
  readings are silently substituted.
- Nord Pool 15-minute spot and FMI HARMONIE hourly temperatures reuse
  the existing PR #1 readers; NVS caches and UTC timestamps preserved.
  `cottage/forecast_bridge.*` produces 96/192 quarter-hour MPC inputs,
  validates price and weather recency and coverage, interpolates hourly
  outdoor observations, rejects bad clocks, NaN and missing slots.
  Configurable tariff factors include daytime/nighttime Finnish
  transmission rates, electricity tax, margin and VAT.
- The independent `CottagePhysics` and `CottageMPC` from PR #4 remain
  unchanged (besides using the forecast bridge): their only simulated
  actions are HEAT 16 °C / OFF. The model is **synthetic** until actual
  cottage identification/calibration. The firmware's default
  `COTTAGE_SYNTHETIC_TEST_MODEL=0` does NOT issue economic plans.
- The local Finnish webpage shows raw connections, temperatures (null if
  absent), heating status, compressor Hz, whole-cottage Shelly power, spot,
  estimated total price, FMI temperature, proposed plan, predicted
  temperature, electric energy/cost, solver runtime and RAM metrics. The
  permanent heading says **VARJOTILA – EI OHJAA LÄMPÖPUMPPUA**.
  HTTP endpoints are GET-only and expose no switch or HVAC writers.
- The price/weather and telemetry clients remain logically separate from
  the realtime solver task. The Python PC read-only `cottage_probe.py` from
  C1 remains available for independent field diagnostics.

## Tests actually run

- `CottageMPC` C++17: 96/192 slots, exact continuous replay, start/stop
  duration, constrained indoor minimum/maximum, terminal heat, invalid
  prices/forecast and infeasible hard frost — **PASS** on Linux PC.
- Supervisor and telemetry: stale/malformed readings, watchdog failure,
  low-temperature safety recommendation, no physical write permissions
  — **PASS** on Linux PC. Simulated command output is not a transport.
- **Misconfiguration guard**: a native C++ allowlist rejects any CN105
  path other than exact GET `/events` and any Shelly base with a path,
  query, userinfo, fragment or non-HTTP scheme. Redirect following is
  explicitly disabled. URL guard test suite: **PASS**. This prevents an
  accidentally configured `Switch.Set` GET path from reaching a relay.
- `CottageForecast`/Ruuvi: 192 UTC slots across Finnish DST, negative spot,
  VAT and marginal costs, price gaps, stale dates, NaNs, RAWv5 temperature
  and unavailable measurement sentinel — **PASS** on Linux PC.
- **Physical ESP32-S3 bench**: six synthetic scenarios 6/6 feasible with
  identical exact replays. After precomputing period-constant COP,
  thermal power and RC factors the optimized real-device times were:
  96 slots **261, 354, 355 ms**; 192 slots **630, 780, 799 ms**.
  Max 799 ms satisfies the 2 s target for this **bench-only** network
  load. ESP32-S3 240 MHz with 8 MB PSRAM. Internal minimum free heap
  245,444 bytes; minimum free PSRAM 7,920,123 bytes; optimizer task
  stack high-water free about 30,244 **bytes** (ESP-IDF units).
  Local Wi-Fi AP and 3 successfully served HTTP requests ran concurrently.
  See [serial evidence](benchmarks/c2-esp32s3-bench-optimized.log).
  The **unoptimized** reference had 96-slot worst 1,326 ms and
  192-slot worst 3,015 ms, so the original 2s target FAILED before
  targeted algorithmic optimization; no costs or constraints changed.
- ESP32-S3 PlatformIO **compile and link PASS**. Arduino ESP32 2.0.17,
  platform espressif32 7.0.1, 16 MB flash and OPI PSRAM selected; RAM
  static utilization ~59,972 B; flash static utilization ~980,985 B after safety URL allowlist.
  **These build values are not a physical ESP32 peak-memory or timing test.**
- Prior C1 verified CN105 SSE and Shelly RPC GET on the live LAN
  (see [C1 evidence](C1-cottage-probe.md)). No commands were transmitted.

## 48-hour independent MILP reference

SciPy/HiGHS on a separate Linux research directory, 12-second solver
limit per case. The exact input and output are in
[JSON](benchmarks/cottage-48h-milp.json).

| Scenario | C++ CottageMPC (€) | MILP best found (€) | MILP status | MIP gap |
|---|---:|---:|---:|---:|
| Mild | 5.98282 | 5.98273 | time limit (1) | 3.46% |
| Cold | 12.04571 | 12.06566 | time limit (1) | 2.52% |
| Price spike | 9.90158 | 9.99369 | time limit (1) | 4.87% |
| Defrost | 6.29764 | 6.29002 | time limit (1) | 3.68% |

All recorded schedules replayed with no simulated thermal/actuator violations.
**48h MILP optimality has NOT been demonstrated**. MILP can have a worse
incumbent than C++ when its search times out. No numerical values here
demonstrate real savings, and none are Mitsubishi manufacturer COP values.

## Known blockers and follow-up test requirements

1. **Not yet validated end-to-end:** the full standalone Wi-Fi shadow
   firmware has NOT been flashed; only the isolated synthetic bench
   variant has been flashed to the test board. CN105 SSE, Shelly,
   Nord Pool, FMI, BLE, actual NVS recovery and the real Web UI have not
   been tested simultaneously on hardware. The bench's Wi-Fi+HTTP
   traffic does NOT substitute for this full workload.
2. **No independent cottage sensor confirmed.** Main-house Ruuvi sensors
   cannot be presumed to measure the cottage. Validate a sensor in
   the coldest at-risk room and its BLE reception and timestamping.
3. **No physical independent freeze protection confirmed.** This is
   necessary for C3. The old Mitsubishi and this software cannot promise
   the cottage stays above 5 °C under all faults/power outages.
4. **Actual building, heat pump COP/capacity, defrost and setpoint 16 °C
   thermostat cycle parameters not calibrated**. The default C2 application
   therefore refuses to present an optimized schedule as reliable.
5. Embedded read-only GET clients and Wi-Fi task reconnects require
   extended network outage, power loss, buffer-exhaustion and watchdog
   tests on hardware. A successful cross-compile cannot substitute for
   these tests.
6. Update process must preserve/recover from interrupted writes, flash
   wear, DST, current tariff factors and clock drift in future C2/C3.
   Current model uses a sampled 15-minute boundary and is not calibrated
   to a partly elapsed starting interval. Before C3, validate this.

## No-actuation proof scope

The added ESP32 firmware uses GET requests for /events and the Shelly
status RPC; it exposes GET-only /api/status and /api/plan. There are
**no HTTP POST/PUT, CN105 commands, IR transmitters or Shelly relay
writes** in this C2 module. This establishes source-level separation,
not a formally verified hardware safety property.

## Reproduction

```sh
# Native CMake build also exercises the existing PR #1 project
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure

# 48-hour independent MILP on a machine with SciPy and NumPy
python tools/cottage_reference.py --exe ./build/cottage_cli \
   --time-limit 12 --output docs/benchmarks/cottage-48h-milp.json

# Cross compile only; DO NOT flash the working CN105 controller
cd firmware/cottage
pio run -e esp32s3_cottage_shadow
```

The local network config is intentionally absent from Git. See
`firmware/cottage/include/cottage_local_config.example.hpp`, and configure
local values outside version control before **spare-board-only** tests.

**Approval gates:** C0 PC model PASS limited to synthetic scope; C1
independent temperature unverified; C2 firmware builds but integrated
device test pending; C3 physical actuation fully prohibited.

## Subsequent bench optimization details

The old MPC evaluated exact `advance()` and exp/COP/available-power
calculations for each candidate at every horizon step, even when those
values were constant for that step. The revised forward search
precomputes the constant RC and COP terms outside the state loop
without relaxing limits, and exact replay still calls the original
`advance()`. Native 96/192 tests and ESP32 serial bench all passed.

Physical bench install affected **only** a USB-connected development
ESP32-S3. The working cottage CN105 bridge and Shelly were not flashed,
configured or sent commands. Source-level write suppression is still
not a substitute for C3's independent fail-safe hardware verification.


## Current source-level hardening

The firmware now validates CN105/Shelly URLs against an exact allowlist,
disables redirects and uses a web-loop heartbeat in the supervisor.
Arduino `enableLoopWDT()` is compiled for the Wi-Fi firmware. The watchdog
and failover behavior have **not** been verified on the full, connected
firmware; the serial bench ran a distinct minimal build.

The full firmware was recompiled after these changes: PASS; approximately
60,076 B static RAM and 980,985 B flash reported by the build system.
