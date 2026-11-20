# Sensor and meter discovery (19.11.2026)

**Status: logic host-tested, firmware builds, UI driven in a browser against
`tools/dev_device_simulator.py`. NOT run on a flashed ESP32-S3, with a real
RuuviTag over the air, or with a real Shelly.**

## RuuviTag list (implemented)

* The BLE callback feeds every decoded, plausible Data Format 5 frame into
  `RuuviRegistry` (8 slots, no allocation). `GET /api/air/sensors` returns the
  nearby sensors with name, role (indoor/outdoor/none), temperature, humidity,
  battery, RSSI, **age** and a `live` flag (advertised within 2 minutes), plus
  the BLE scan counters and the bound MACs.
* Sensors unseen for 15 minutes disappear; a silent sensor keeps its old age and
  is shown as "not heard", never as fresh. A bound sensor that stopped
  advertising stays visible with a warning (optimization does not use its old
  value; that rule already existed in `RuuviBinding::valid`).
* **Names** (`POST /api/air/sensor-name`, 8 slots, 23 bytes, UTF-8 without markup
  characters) are labels only, stored in their own NVS key; RAM is restored if the
  flash write fails; a corrupt stored table yields no names, never junk.
* **Roles** use the existing `POST /api/air/bind` (indoor / outdoor / none).
  Replacing a bound *indoor* sensor discards the old sensor's history (existing
  behaviour), so the UI needs a second explicit tap that says so.
* Memory: the registry and names are < 1 KiB; the BLE scan itself and its
  memory gate are unchanged.

## Shelly (implemented, unverified against a device)

* `GET /api/shelly/discover` starts/polls one worker task (mDNS `_shelly._tcp`,
  plus `_http._tcp` hosts named `shelly…`), then reads `/shelly` and, for
  supported devices, `/rpc/Switch.GetStatus?id=0` for a live power figure. The
  web loop is never blocked; scans are limited to one per 8 s.
* Only **private IPv4** addresses become meter URLs. Responses must be small
  (Content-Length ≤ 4 KiB) and must parse as a Shelly identification document,
  so an arbitrary web server on the LAN is never offered.
* Supported = what the firmware reader really uses: Gen2+ without a password.
  Gen1 and password-protected devices are **listed with the reason**, with no
  "use" button.
* **Scope:** the owner states what the meter measures (pump only / whole house /
  unknown). Unknown is the default and a changed address resets it. Only
  `heat_pump` may feed the pump consumption/COP model
  (`shelly_may_feed_pump_model`); the wizard suggests switching the pump on and
  off to see the reading follow. The relay is never switched.
  Today no code path feeds the Shelly power into a COP/consumption model (it is
  display data); the gate and the owner's statement are in place for when one does,
  and the main dashboard's consumption tile does not yet show the scope.
* The test documents follow Shelly's *published* API format; they are **not
  recordings** from a device.

## Midea (not implemented)

No LAN discovery exists in the repository and the Midea discovery datagram could
not be verified without a unit, so nothing was added. The UI still points to the
existing Midea card in Settings.

## Evidence

| Check | Result |
|---|---|
| `ruuvi_registry_tests` (official Ruuvi DF5 vector, eviction, ageing, names) | pass |
| `shelly_discovery_tests` (Gen1/Gen2/auth/not-Shelly, power, hostnames, LAN-only URLs, scope) | pass |
| `site_settings_tests` (`shelly_scope`, reset on address change) | pass |
| `tests/test_setup_wizard.js` (list text, confirm rule, meter fields, summary) | pass |
| Firmware `esp32s3_cottage_shadow` | SUCCESS (+≈50 KiB flash for mDNS query/HTTP client) |
| Browser, 375 px: list, naming, role bind, replace confirm, markup name refused, stale sensor, Shelly scan, unsupported reasons, scope, polling stops on leaving | works on the simulator |

## Open

* No real RuuviTag/Shelly/pump test; mDNS behaviour of real Shelly firmware
  versions is unverified. BLE scan cadence may make a sensor take up to a minute
  to appear.
* Shelly Gen1 and password-protected devices are unsupported; Midea discovery is
  missing; a sensor *name* does not yet appear on the main dashboard.
