# Standard ESPHome CN105 gateway: defrost-aware control

## Evidence and scope

The gateway's climate action and optional `text_sensor/Sub Mode` can
report defrost. See [MitsubishiCN105ESPHome's description of Sub Mode and
Stage](https://github.com/echavet/MitsubishiCN105ESPHome#auto-and-stage-sensors).
Stage is fan speed, not a defrost detector. A positive reported defrost
may defer normal automatic changes to a pump already requesting HEAT.
This does not prove a fresh UART measurement, source sequence, compressor
runtime or defrost capability. An empty or absent diagnostic is **unknown**;
ordinary HEATING/IDLE and compressor frequency are not proof that defrost
is absent. The controller works with unchanged standard gateway firmware.

For the FD series the outdoor controller owns defrost, protection,
reversing valve, compressor and fan control. The
[OBH489 service manual](https://library.mitsubishielectric.co.uk/pdf/download_full/1636)
describes the dedicated defrost thermistor. The FD operating instructions
describe defrost interruptions of up to ten minutes. The control rule is
not an attempt to reconstruct this internal algorithm or infer a physically
safe shorter heat pulse from the three-minute restart protection.

## Implementation

* Observe the existing SSE feed and validated climate GET. Read the optional
  Sub Mode entity once per existing heating tick and before a normal change
  to an already heating pump. No new task, watchdog, lock, API write, or
  gateway firmware requirement. Optional reads use existing network memory
  limits, bounded HTTP timeouts, bounded payload and no retries.
* Positive observations from independent Sub Mode and climate action
  channels cannot clear one another. A same-channel explicit other state
  releases the report after a one-minute recovery interval. Ignore an older
  in-flight GET when a more recent event has arrived. Unknown data neither
  clears nor refreshes a positive report. Hold up to twelve minutes from
  the first positive report (engineering grace, not UART verification).
  Repeated cached reports cannot restart the timer. A report beyond that
  limit is overdue; use original dwell, never claim a completed defrost.
  Separate per-source report starts, bracketed ends and receipt-duration
  counters distinguish actual observed state changes from repeated polling.
* Both the arbiter and final pre-POST authorization defer normal automatic
  changes during the reported defrost/recovery. This is not a failed
  physical command, not a power cut, and does not arm the controller.
  Manual pump control remains with the pump. The existing 30-minute
  HEAT/OFF and OFF/HEAT minimum is unchanged.
* Reject pre-defrost plans even when the requested mode returns unchanged.
  Exclude observed defrost/recovery intervals from normal thermal/electric
  fitting and walk-forward windows; never manufacture normal heating
  output or inferred start/stop events across those intervals. Count
  rejected intervals separately since boot, without changing saved model
  blob format or resetting stored calibration.
* Show reported/unknown/recovery state, report count, and learning exclusion
  in status/API/dashboard. The defrost UART verification field stays false.

## Field validation before adapting dwell

Observe natural heating and defrost cycles: independently establish that
the optional report becomes non-empty during heating, actually reports
defrost start and completion on the installed model, and verify it agrees
with independent observations. No synthetic positive report is a field
test. A pump with a minimum HEAT setpoint above the desired room target
may have an infeasible temperature band with a 30-minute minimum on-time.
Neither changing the setpoint below its advertised minimum nor shortening
the minimum solely because of temperature rise is justified. Shorter
adaptive heating intervals require model-specific evidence that compressor
protection and frost management remain effective, separate approval and
consistent MPC and command constraints. This PR does not activate that
untested policy or claim that room-temperature overshoot is solved.
