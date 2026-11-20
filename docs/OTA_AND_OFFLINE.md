# OTA summary and offline behaviour (19.11.2026)

**Status: logic host-tested, firmware builds, UI driven in a browser (375 px, FI/EN)
against `tools/dev_device_simulator.py` whose `/api/freshness` runs the real
assessment code. NOT run on a flashed ESP32-S3; no signed update was performed.**

## Deliberately unchanged

The signed OTA trust chain - manifest signature, TLS, size/SHA-256, A/B slots,
rollback, boot confirmation, manifest quarantine - is **not touched**
(`cottage_secure_ota.*`, `ota_recovery.hpp`, the publish/server tools: zero diff
against `main`). Nothing was flashed and the field board was not used.

## Firmware update card (settings)

* A one-sentence **summary** from the device's own OTA fields: no channel built in,
  queued, downloading (KiB), installed/restarting, waiting for network/clock,
  temporary resource error, check/download failed (with what to check), not checked
  yet, up to date (confirmed release), or *"the detected release cannot be installed"*
  (already installed, rejected earlier, or awaiting confirmation).
* **Last successful update** (time and release) from the persisted event log:
  `HistoryEventKind::UpdateConfirmed` is written when the confirmed-good version
  rises above a previously seen non-zero value (a 0 reading is "not loaded yet" and
  never counts, so a normal reboot logs nothing). A device with no prior confirmed
  version cannot log its first update; the card then says *unknown*, not a guess.
* **Release notes:** the signed manifest carries only version, size and SHA-256, so
  notes cannot be shown on the device. The card says so and links to the GitHub
  releases page. Adding notes to the manifest would change the signed format and
  the publishing tools and needs its own reviewed change.
* **"Check" and "install" stay one action**, as in the existing worker: it fetches the
  manifest and installs an eligible release in the same pass, so the device never
  *holds* an "update available" state. The card explains this instead of showing a
  second button that would imply otherwise.

## Data freshness and offline behaviour

`GET /api/freshness` and a Home card report, per source, its state, its own last good
time and expiry, and what the controller does as a result. The assessment
(`cottage/data_freshness.hpp`) uses the controller's own rules:

| Fact | Effect (identical to the controller, tested against `bootstrap_price_decision`) |
|---|---|
| Prices fetched < 36 h ago, valid ≥ 2 h ahead, trusted clock | price control |
| Prices ending in < 2 h, expired, > 36 h old, no clock, none yet, or a fetch error | room is a plain thermostat on target ±0.5 °C |
| No fresh indoor measurement | no heating proposal at all; the pump keeps its own controller |
| Wi-Fi/Internet down | nothing changes until cached data expires; the card names the expiry time and what follows |

Every source is shown as ok / short / stale / expired / missing / not set up / error
with the reason code, and "last successful data fetch" keeps the **old** time during an
outage. Old prices or forecasts are never presented as current. Weather validity does
not gate the proposal (the controller does not use outdoor temperature).

## Evidence

| Check | Result |
|---|---|
| `data_freshness_tests`: healthy; 12/19.5/21/31 h offline; the 36 h rule (incl. a future fetch time); missing/unconfigured/error with last-good time kept; sensor/pump; untrusted clock; JSON - and in every scenario the predicted fallback equals the real controller's behaviour | pass |
| `history_tests`: update event rules (no false event at boot or on a 0 reading), 32-bit arg through flash and JSON | pass |
| `tests/test_health_view.js` (every source state, overall statements, every OTA state, last-update text, 80 literals + static captions in English) | pass |
| Existing Node/Python tests (now load the helper script blocks) | pass; Python failing set = `main`'s 5 |
| Firmware `esp32s3_cottage_shadow` | SUCCESS |
| Browser: healthy, offline 12 h / 19.5 h / 21 h, no sensor, price-service error, every OTA state, FI/EN | works on the simulator |

## Open

* No physical signed-update or rollback run (same open item as before); F09 evidence is
  still outstanding.
* Release notes in the manifest, a separate "check only" mode, and an update history
  list are not implemented.
* The first update of a device with no confirmed version is not logged.

## Legacy events.bin migration and real command gate

Previous devices stored history events as an 8-byte event plus 2-byte CRC
(record_size=10); OTA confirmation added a 32-bit argument and increased the
disk record to 14 bytes. The loader accepts both valid disk sizes, checks each
legacy CRC, copies the original UTC/kind/source/value to the new type with
`arg=0`, retains valid records before a torn tail, and atomically rewrites
the event log to the new format on its next successful flush. Existing boot,
outage and command records are **not deleted just because firmware changed**.
Host tests construct actual old-format event bytes and verify migration,
reloading, CRC and a torn old-format tail. This is not a physical flash test.

The freshness report exposes the controller's current `control_gate` value,
which is obtained from `cottage_control_tick` rather than inferred from the
optional SSE/UART telemetry snapshot. Stale auxiliary telemetry is shown
separately and does **not** imply that a CN105 path using successful direct
HTTP GET/readback cannot send commands. Conversely, a fresh telemetry snapshot
never proves that commands are authorized: disabled, no-proposal and unverified
gates are explained as *suggestion only*. Neither report changes or bypasses
the existing controller safety decisions.
