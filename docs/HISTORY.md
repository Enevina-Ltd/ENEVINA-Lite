# History and charts (19.11.2026)

**Status: logic host-tested (ring, recorder, queries, persistence with torn writes),
firmware builds, UI driven in a browser (375 px, FI/EN, 24 h / 7 d / 30 d) against
`tools/dev_device_simulator.py`, whose `/api/history` runs the REAL recorder →
store → file → reload → query pipeline on recorded prices with a synthetic room.
NOT run on a flashed ESP32-S3: LittleFS mount/format, flash wear and the hourly flush
are unverified on hardware.**

## What is recorded

One record per 15-minute slot (20 bytes, 30 days = 2880 slots = 57.6 KiB in PSRAM):
indoor temperature and humidity (fresh RuuviTag only), outdoor temperature, the
pump-reported setpoint, average power of the metered load, total price per kWh in
the zone currency, the controller's proposal (off / heat / hold), whether automatic
control was armed, and pump/Wi-Fi presence flags. A separate ring keeps the last 128
**events**: boot (with reset reason), loss and return of Wi-Fi / pump data / room
sensor / prices (with the outage duration), and counters of commands sent,
acknowledged and failed.

## Rules that keep it honest

* **Missing stays missing.** No interpolation, no carry-forward, no zero fill: a
  field with no valid sample in the slot is a sentinel, a slot with no samples at all
  produces no record, and the charts break the line at every hole.
* **Provenance is stored.** The outdoor value is the outdoor sensor when one is
  bound, otherwise the weather *forecast* with a flag; the chart draws forecast
  dashed and labels it. The controller's proposal is shown separately from "armed":
  commands go out only when automatic control is on.
* **Trusted clock only.** No clock, no record; a clock that jumps backwards is ignored
  until it passes the last slot. The open slot is never stored early.
* **Events are debounced** (state must hold 60 s) so a flapping link does not spam
  the log; a reboot re-bases the command counters instead of faking events.
* **Energy** is integrated only between successive, independently captured Shelly
  measurements within the same 15-minute slot (trapezoids, gaps longer than 180 s
  are not integrated). The 15-second history polling loop does not turn the same
  old Shelly capture into another observation. Coverage is conservatively
  reported as whole verified minutes; historic records without coverage metadata
  are treated as unknown, not full-slot readings. A single 1000 W reading at
  14:50 does **not** imply 0.25 kWh for the whole slot. The card reports verified
  power coverage and the measured target (pump only / whole house / unknown).
  The scope is stored in each record (flag bits 6-7), so changing the meter or the
  owner's answer later never relabels readings taken earlier. `meter_scope` in the
  response is what the meter measured over the returned range; `current_meter_scope`
  is only the present setting. If the target changed inside the range the response
  says `mixed`, energy is not summed (`kwh: null`), and a slot whose meter changed
  halfway has no power value. Forecast provenance distinguishes measured,
  forecast and mixed values within one slot and across multi-slot summaries.
* **Currencies never retroactively change.** Each valid price observation carries
  its immutable ISO currency code inside the persisted 20-byte history record.
  Legacy records without a code remain `unknown`; multi-currency windows are
  labelled `mixed` and have no cross-currency average. Charts do not place mixed
  currencies on one axis; CSV rows preserve each observed currency separately.
  `current_currency` describes the present contract, not historical data.
* The UI reports both record coverage and **verified power-measurement minutes**
  and warns about gaps, unknown/mixed price currencies, or missing persistence.
  Estimated energy from uncovered periods is not invented.

## Flash use (bounded)

* Storage: the otherwise unused `spiffs` partition (3.4 MB in `default_16MB.csv`) via
  LittleFS, mounted read-write; it is formatted **only if the mount fails** (never
  formatted), and nothing else lives there. NVS, OTA slots and settings are untouched.
* Records are **appended hourly** (4 records, ~88 bytes); new events are flushed at most
  every 10 min; each record has a CRC-16. The file is compacted (atomic temp + rename)
  only after 240 extra records (every ~2.5 days), streamed in 968-byte chunks so no
  file-sized buffer is needed. Host test: 120 days of writes keep the file below ring +
  slack and the traffic far below the partition (< 30 kB/day on average).
* A power cut can lose at most the last hour. A torn tail is cut at the first bad CRC on
  boot and repaired by a rewrite; a failed append never advances the persisted position;
  a foreign or old-format file is replaced, never interpreted.

## UI

New *History* view: range buttons, coverage line, temperature chart (indoor, outdoor
measured/forecast, stepped pump setpoint, proposal lane with armed markers), price bars,
measured power bars, summary tiles, event list and **CSV export** of exactly what is
shown (empty cells for missing values). Touch/hover tooltips; phone and desktop layouts;
night theme; every text in Finnish and English.

## Evidence

| Check | Result |
|---|---|
| `history_tests`: ring monotonicity/eviction, recorder, timestamp-unique Shelly samples, non-full-slot energy integration, historical currency separation, forecast/measurement provenance, JSON validity, persistence round-trip, torn append, corruption, and bounded flash growth | pass (host) |
| `tests/test_history_view.js` (validation, gap-breaking segments, forecast/measured split, extents, events, coverage, energy wording, CSV, ticks, 20 literals + static captions in English) | pass |
| Host C++ Debug/Release, Node, Python failing set (= `main`'s 5) | pass / pass / identical |
| Firmware `esp32s3_cottage_shadow` | SUCCESS (LittleFS + PSRAM ring + collector task) |
| Browser: 24 h (100 % coverage), 7 d (95 %, line broken at the 4 h power cut, events listed), 30 d (22 % → warning), FI/EN, tooltip, CSV | works on the simulator |

## Open

* **Hardware verification:** first-boot format of the history partition, mount time,
  hourly flush duration under the data lock, and long-term wear are untested.
* Humidity is recorded but not yet charted; past prices before the first record and
  weather *observations* (as opposed to forecast) are not available.
* The older hourly indoor history (NVS, 168 h) remains for the Settings sparkline; the two
  are not merged.
