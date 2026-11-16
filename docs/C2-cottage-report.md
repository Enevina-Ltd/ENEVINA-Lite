# C2 — Mitsubishi FD25VA read-only Wi-Fi shadow, implementation report (2026-11-16)

**C2 STATUS: NOT APPROVED FOR LIVE PILOT.** Firmware compiled successfully for
ESP32-S3 with PlatformIO/Arduino, but it has **not yet been flashed on a
confirmed standalone development board or tested with all live network
services concurrently**. No physical actuator write command exists in code.

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
- `CottageForecast`/Ruuvi: 192 UTC slots across Finnish DST, negative spot,
  VAT and marginal costs, price gaps, stale dates, NaNs, RAWv5 temperature
  and unavailable measurement sentinel — **PASS** on Linux PC.
- ESP32-S3 PlatformIO **compile and link PASS**. Arduino ESP32 2.0.17,
  platform espressif32 7.0.1, 16 MB flash and OPI PSRAM selected; RAM
  static utilization ~59,972 B; flash static utilization ~959,105 B.
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

1. **Not physically verified:** standalone ESP32-S3 Wi-Fi firmware flash,
   real device network traffic, simultaneous CN105 SSE + Shelly + price +
   FMI + web + BLE + MPC, 96/192 runtime and PSRAM/internal heap/stack
   on the device. Requires an identified spare ESP32, not the
   currently working CN105 bridge.
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
