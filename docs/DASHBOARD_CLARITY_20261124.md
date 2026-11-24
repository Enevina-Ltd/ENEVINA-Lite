# Consumer dashboard clarity

The default home view presents room temperature and target, observed
pump request (not inferred compressor runtime), current total electricity
price and a short explanation. Separate colour-coded charts show recorded
indoor temperatures, future prices, a valid calculated heating plan, recorded
power and coverage-qualified energy/cost. All existing controls and technical
details remain accessible; historical indoor/outdoor charts are split, and
the former combined technical plot is in an expandable section.

## Display contracts

- Price bars have a real zero baseline (including negative and zero prices).
  Colours use the firmware's own total-price classes, never an independently
  invented threshold. If the tariff/classification is unavailable, a region-
  matched spot-price graph is explicitly unclassified.
- Missing readings are not interpolated or filled with zero. The current
  target/comfort band is labelled as current, not historic target data.
  Weather forecasts are dashed. The keyboard/touch range control provides
  exact values and an accessible alternative to colour-only interpretation.
- A heating schedule is displayed only when the device returns a valid
  current-slot plan whose computation timestamp agrees with the latest
  status and live MPC validity. Planned HEAT means requested heating, not
  actual compressor runtime. Without such a plan, the UI says its next
  heating time is unknown; price windows never become fictional commands.
- Home history is an explicitly rolling 24-hour range (not a calendar day).
  Scope and measured coverage accompany energy/cost; mixed or unknown meter
  scope is not relabelled or summed. Only verified pump-meter/price coverage
  produces a pump cost estimate.
- Pump mode is a fresh observed request from the common backend adapter.
  Optional gateway compressor readings and defrost reports retain their
  gateway provenance. No controller decisions or permissions change.
- No CDN, new web endpoint, external dependency at runtime, new credentials,
  new task or lock. Existing status, prices and plan endpoints gain small
  additive presentation metadata. Home polls its 24-hour history once per
  three minutes using the existing history endpoint, in place of the
  settings-only Ruuvi history refresh while Home is selected.
- FI/EN and automatic night theme remain supported.

## Regressions

    node tests/test_dashboard_clarity.js

Negative/zero price geometry, missing samples, accessible inspector
selection, plan validity and market identity, meter scope, and
language/status semantics. Included in CI.

    CHROMIUM_BIN=/path/to/chromium CLARITY_SCREENSHOTS=/tmp/clarity node tests/browser/dashboard_clarity_smoke.cjs

Uses the actual full firmware HTML with a localhost synthetic API fixture
and Chromium DevTools (Node 22+). Optional screenshots are for visual QA,
not production measurements. Checks desktop 1280x900 and mobile 390x844,
no horizontal overflow, FI/EN, night theme, actual SVGs, keyboard slider,
unavailable/active/rejected plans, detail navigation, offline state, no
unexpected JavaScript exceptions or POST requests.

Production remains on its earlier signed release during the 72-hour
observation. This is browser/firmware regression evidence, not user testing
or a claim of a deployed dashboard.
