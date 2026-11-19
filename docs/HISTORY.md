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
* **Energy** is the sum over slots that actually had a power reading and states how
  many; it is unknown (not 0) with no readings, and the card says what the meter
  measures (pump only / whole house / unknown, from the owner's `shelly_scope`).
* The UI states coverage ("recorded for 95 % of the time") and warns below 80 % or
  when persistence is unavailable.

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
| `history_tests`: ring monotonicity/eviction, recorder (averages, gaps, clock rules, implausible values), event debounce and command counters, series/summary/coverage, JSON validity (`kwh:null` when unmetered), persistence round-trip, torn append, flipped byte, foreign/old header, duplicate slots, failing append/replace, 120-day bounded growth | pass |
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
