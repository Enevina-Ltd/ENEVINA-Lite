# Below-minimum CN105 maintenance heating

## Evidence and scope
A small insulated building held below the native HEAT minimum can exceed
its room target long before a fixed 30-minute HEAT request has finished.
A 15-minute OFF minimum is an installation-test candidate, not a universal
manufacturer optimum. The Mitsubishi Electric FD-series service literature
documents a three-minute compressor restart delay (OBH488A, section 9-12).
Do not disable or modify that protection or the unit's own controller.
The field trace motivating this work used a gateway request and independent
room measurement; the power-meter scope was unconfirmed. It cannot prove
heat-pump COP, optimal cycle length or cost savings.

Only the CN105 ESPHome adapter, Hold mode, independently selected Ruuvi
temperature and a requested room target below the existing 16 C device
minimum select this policy. Ordinary modes and all other adapters keep
their previous 30-minute dwell and commands.

## Policy
- Start at the existing target-minus-0.5 C threshold or a colder explicitly
  allowed expensive-period floor. No start inside the temperature deadband
  merely because electricity becomes inexpensive. Request native HEAT 16 C.
- Stop at target, or the profile's explicitly allowed ceiling/floor as
  applicable, with at least 0.5 C hysteresis. A genuine measured target
  stop can end the request before the dwell. A price-only stop below the
  actual room target cannot bypass the scoped 15-minute mode dwell.
- OFF-to-HEAT requires 15 minutes since the existing observed/request
  mode change in this scope. Observation gaps retain conservative timing;
  missing or stale temperature cannot supply the exceptional stop.
- The same helper is used at proposal, pre-POST, refreshed proposal and
  final dispatch checks. Existing authorization, generation, live
  readback, defrost/recovery handling and uncertain-command reconciliation
  remain in place; no new task, lock, watchdog, NVS schema or command.
- The calibrated full-slot planner uses 15-minute minimum mode lengths
  and only native-minimum heating in the selected scope. An earlier
  measured target stop or a new HEAT request inside the deadband invalidates
  a conflicting current-slot plan. Plan/API/dispatch share plan_validity.
  The live controller can stop sooner than the 15-minute model; this is
  not a claim of a calibrated sub-slot cost optimum.

## Validation
Unit tests exercise neutral/cheap/expensive/no-price thresholds, stale
and nonfinite temperature, changed scope, price-only OFF, 15-min restart,
30-min legacy behavior, authorization, a conflicting MPC plan and defrost
precedence. Firmware source tests pin both the selection and final dispatch
wiring. The original single-transition identification contract is retained:
intervals with ambiguous or multiple transitions are rejected rather than
fabricated. Sub-slot identification and pump-only metering require further
field evidence before comparing actual cost of short and long cycles.
