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
