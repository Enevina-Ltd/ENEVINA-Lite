# Setup wizard (home-LAN steps) – 19.11.2026

**Status: implemented in the flash-backed dashboard; unit-tested in Node and driven
end to end in a real browser against `tools/dev_device_simulator.py`. NOT yet run
against a flashed ESP32-S3 and NOT tried on a real phone.** Wi-Fi onboarding
(phase 1, `/setup`, see `ONBOARDING_WIFI_FIRST_20261118.md`) is unchanged and runs
first. This wizard is phase 2 and opens on the home LAN. The Midea connection
and operating-mode changes below have Node and firmware-build evidence.

## Behaviour

| Step | Saves | Notes |
|---|---|---|
| 1 Language and country | `language`, `country` | Country list comes from `/api/regions` (the firmware table); one zone is implied for single-zone countries, never guessed for SE/NO/DK. |
| 2 Location | `city`, `fmi_latlon` | City search with suggestions (`/api/geocode`, ≥3 characters, debounced, polls while the device worker runs). Manual `lat, lon` with dot decimals; invalid text blocks *Continue* instead of silently keeping an old location. Skippable. |
| 3 Market area | `price_zone` | Labels like `Sweden – Stockholm (SE3)` / `Ruotsi – Tukholma (SE3)`. Required, no default for multi-zone countries. |
| 4 Contract | tariff keys, `tariff_confirmed` | Amounts in the zone currency's minor unit (snt / öre / øre / gr). Nothing is pre-filled with invented values; *Skip* leaves the contract marked unconfirmed. |
| 5 Heat pump | `pump_adapter`, `cn105_url`, local Midea credentials | Select the connection. For Midea, discover a compatible appliance or enter its address and device ID. V2 uses local discovery details. V3 also needs the device's existing local token and key. Save and verify a fresh state read before continuing. |
| 6 Temperature and optimization | Operating mode, room target, comfort limits and profile | Select the mode and save it on this screen. Set the room target and choose the profile. The existing mode endpoint verifies the save. |
| 7 Sensors and meter | RuuviTag binding, `shelly_url`, meter scope | Scan for sensors or enter the details manually. Confirm whether the optional meter measures the heat pump. |
| 8 Summary | `setup_complete`, `setup_step` | Live checklist: ok / needs attention / not set / optional, with the real reason (e.g. price service error codes). The dashboard opens even if items remain. |

* Every step persists `setup_step` on the device, so closing the tab or reloading
  resumes where the owner left off; once `setup_complete` is true a reload never
  restarts it. Settings → *Open setup again* re-runs it without erasing anything.
* Devices upgraded from the Finland-only firmware are migrated as already set up.
* The dashboard now follows the configured country: time zone for clocks, chart
  times and the automatic night theme; currency unit in every `snt/kWh` label; the
  price card shows `Nord Pool · <zone> · <currency>` and, when fetching fails, the
  real reason.
* All wizard text is Finnish with a complete English dictionary entry
  (`tests/test_setup_wizard.js` fails if a literal or caption is missing).

## Evidence

* `node tests/test_setup_wizard.js`: coordinates, tariff parsing, zone labels,
  resume point, fresh Midea state, unsaved connection drafts, operating-mode
  selection and the summary rules.
* Browser run (Chromium pane, 375 px phone and desktop, day and night theme) on the
  simulator: Finland→Sweden→Malmö search→SE4→SEK tariff→Mitsubishi→summary→finish,
  reload resume, reload after completion, Estonia single-zone, invalid coordinates.
* The simulator is a **test harness**, not firmware: it replays recorded Photon /
  Nord Pool fixtures and uses simplified validation. Firmware rules are tested by
  `site_settings_tests`, `market_zones_tests`, `geocode_tests`.

## Not covered / known gaps

* No physical device or phone test; the device-side `/api/geocode` worker and TLS
  to the geocoder have never run on hardware.
* Midea discovery and the new operating-mode step still need a physical ESP32-S3
  and phone browser test. The Midea connection test performs a read. Automatic
  control remains an explicit choice in Settings.
* OpenStreetMap attribution for city search still has to be added before release.
