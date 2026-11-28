# Energy-first maintenance heating

Maintenance (Hold) with an independently selected room sensor, a confirmed
whole-building electricity meter and the Saving profile can use wider
heating/cooling cycles on the standard Mitsubishi CN105 gateway. Ordinary
occupied heating, other profiles, other adapters and unconfirmed meters
retain their previous behavior.

For example, a 12 C target starts heating at 11.5 C. The initial experiment
allows a 15.8 C stop threshold and uses the pump's native 16 C setpoint.
The controller also evaluates the narrow target threshold and the middle
of the available temperature range. The configured comfort upper limit,
if present, still caps the working threshold. No arbitrary fixed on-time
is imposed. The existing minimum restart interval, independent room
sensor, command authorization, observed-state readback and defrost
protection remain in force.

The bounded cycle learner compares complete heat + rest intervals from
one observed own HEAT start to the next, with energy-counter differences
after subtracting the independently learned, stable OFF baseline.
Only continuous, fresh, matched meter/request observations with
acknowledged own transitions enter the comparison. Both room-temperature
boundaries must be near the same target and within 0.5 C of one another.
Comparisons use recent cycles within 1.5 C outdoor temperature; at least
two matched cycles of each alternative are required for selection.
A materially changed qualified baseline (more than 25 W or 15 percent,
whichever is larger) clears completed cycle comparisons and rejects an
open cycle, including after a data gap. The comparison does not mix
different internal heat-gain regimes.
The winner minimizes average incremental kWh per elapsed hour; a
difference below 5 percent does not favor a hotter alternative. An
occasional alternative cycle allows further learning. The last cycle's
cost is an estimate using sampled marginal electricity prices; the
empirical selection minimizes energy, not historical tariff differences.

After the existing thermal and electrical models are validated, monetary
MPC uses the widened permissible band and current forecast prices.
A relative runtime proxy alone cannot activate monetary maintenance MPC.
Defrost and hard limits continue to override the economic proposal.

The per-cycle comparison is RAM-only in this version and is relearned on
reboot; the existing persistent thermal and electrical model is retained.
A simulated comparison is not proof of savings in a real installation.
The 15.8 C model limit is a stop/forecast threshold, not a promise that
post-stop residual heat cannot briefly raise room temperature above it.

## Forecast-first maintenance timing

The Saving maintenance path gives actual marginal electricity price priority
over exploratory cycle-size experiments. It uses contiguous known tariffs
(up to 24 hours), hourly outdoor forecasts and the existing locally fitted
thermal response. A bounded event-based dynamic program compares complete
charges at the selected economical ceiling, including the measured startup
delay, the intervening OFF cooling and a terminal temperature-restoration
obligation. Only a shift supported by both nominal and faster-cooling /
lower-output estimates may change the live proposal. Real temperature limits,
observed gateway state, minimum restart dwell and defrost protection still
override price advice. The solver is provisional: its objective is
price-weighted HEAT-request time, not validated kWh or a guaranteed euro
saving. It does not bypass calibrated monetary MPC. A stale model/forecast
cannot authorise a long-range shift; an imminent material price rise can
use the conservative near-target fallback. During a material expensive
period, completed qualified energy comparisons take priority over exploration.
Price-directed cycles are excluded from the temperature-only comparison.

## Time-limited strategy comparison

The local controller can run a time-limited comparison in armed Hold mode
below the adapter's minimum heating setpoint. The owner supplies the same
lower and upper room limits and the trial duration for each strategy. The
pump is commanded at its own supported minimum heating setpoint. The
controller uses the current profile's existing response margin inside the
supplied limits. A trial can run for up to seven days and expires on the
earlier of the wall-clock and monotonic deadlines. Reboot starts the usual
policy. Changes of site, pump link or control authorization invalidate a trial.
The existing supervisor and command readback govern dispatch.

| Strategy | Policy |
| --- | --- |
| Short | Existing narrow Hold swing, with the approved limits. |
| Thermostat | Hold the heat request past the room target. Consider OFF after two distinct, stable, pump-origin observations of compressor stop near the pump's setpoint. |
| Modulation | Also consider OFF when sustained pump-relative compressor and measured electrical demand drops agree with slowing room warming. |
| Forecast | Use the existing price and thermal planner within the approved envelope; degrade to the temperature policy where those inputs are insufficient. |

A standard bridge can heat through the pump's own setpoint and collect
complete-cycle data under the approved room limit. When pump phase is
unknown, the trial holds the native heat request until the approved working
upper threshold. A limit-triggered stop is reported separately and provides
no evidence of a thermostat or modulation stop. A CN105 ESPHome receiver accepts optional Boolean
`defrost` and `fault` fields only in the proven UART poll envelope. The
producer must tie each populated field to a decoded reply in that poll.
A missing field remains unknown. A cached SSE value or climate GET cannot
authorize a phase-based stop. A positive defrost report blocks a change
during defrost and recovery through the normal supervision path. If the
pump keeps running in cold weather the phase engine can identify sustained
modulation, continue the heat request or reach the approved upper threshold.

The local endpoint POST `/api/control/cycle-trial` uses the existing settings
token and accepts form fields `strategy`, `lower_c`, `upper_c`, and integer
`minutes`. Strategies are `short`, `thermostat`, `modulation`, `forecast`,
and `automatic`. Send `strategy=existing` to end the trial immediately.
Automatic selects on activation from recent matched cycles, requiring two
comparable cycles for both the short benchmark and any chosen alternative.
Forecast is the fallback until comparable data exists.

Per-trial comparisons reuse the existing shared-meter cycle learner, with
fresh meter, stable measured background load and confirmed HEAT/OFF
transitions. Whole-building energy comes from the meter counter. Pump energy
is estimated by subtracting the qualified baseline. Tariff costs use
interval estimates. Startup and reported-defrost energy are only shown
where those operating phases were available. Indoor-minus-outdoor degree
hours describe the thermal exposure. They do not measure heat flow in kWh.
A cycle with different thermal endpoints or excursions beyond the approved
limits is excluded. A phase-based cycle that stopped at a temperature limit
can be displayed but cannot qualify as evidence for the phase-based strategy.
Trial evidence is RAM-only. The existing maintenance checkpoint format and
persistent thermal calibration are retained. Field savings require
equal-envelope, repeated measurements in comparable weather and tariff
conditions, with adequate coverage of each strategy.
