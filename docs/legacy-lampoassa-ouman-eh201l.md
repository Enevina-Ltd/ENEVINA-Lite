# Legacy compatibility candidate: Lämpöässä T-series / Ouman EH-201/L

**Status:** research and simulation candidate only. **Not tested, not certified, not electrically connected.** Do not enable sensor emulation on a real unit based on this note.

## Scope and evidence

Candidate: older **Lämpöässä T8–T15**, especially early 2000s models with **Ouman EH-201/L** control and **TMO outdoor thermistor**. The 2001 T8–T15 manual also documents versions with **Honeywell thermostat** (no automatic heating regulation) and **EH-203/G**; do not assume that every unit has EH-201/L or the same sensor.

Sources:
- [Lämpöässä T8–T15 manual 5/2001](https://oilon.com/wp-content/uploads/2021/03/Lampoassa_T_P_8-15_2001_05.pdf), p. 4 and p. 6.
- [Ouman EH-201/L manual](https://kauppa.ouman.fi/wp-content/uploads/2026/01/EH-201L_kayttoohje_v.1.51_LQ.pdf), print pp. 4, 10, 26–28 and 33–35. **Version-specific documentation must be checked against the actual controller firmware.**

## Outdoor sensor evidence for EH-201/L

Ouman calls the separate outdoor sensor **TMO**, measured on the outdoor temperature input (measurement 1). Its manual gives the following *example values for this specific controller's sensor family*, not a universal heat-pump thermistor curve:

| Outdoor temperature °C | Resistance Ω |
|---:|---:|
| -20 | 96 890 |
| -10 | 55 340 |
| -5 | 42 340 |
| 0 | 32 660 |
| +5 | 25 400 |
| +10 | 19 900 |
| +20 | 12 490 |
| +25 | 10 000 |
| +30 | 8 064 |

The complete Ouman manual has the official table; **do not extrapolate or apply it to EH-203/G, ÄssäControl, Honeywell, later models, or any unit whose sensor is not verified.** Measure/discover actual sensor type, voltage/current, fault reaction and connector before choosing a resistor-network emulator.

This controller's heating curve is defined with points at -20 / 0 / +20 °C and controls supply water via a mixing valve. Inverting its calibrated curve could in principle control supply target indirectly by making the controller see a fictitious weather temperature.

## Important: heat generation is not always governed by the outdoor sensor

EH-201/L manual distinguishes:
- **Full-output geothermal / storage tank:** compressor may run according to a separate user-configured storage tank target (`MaalämVaraaja`) and hysteresis. Changing fictitious outdoor weather primarily changes **mixing valve supply-water target**, and may not reliably schedule compressor power.
- **Partial-output geothermal:** compressor can also respond to the heating-circuit supply target and storage tank lower sensor. Outdoor-input emulation could indirectly influence compressor activity, **but only after validating actual controller configuration and response**.

Consequently do **not** present sensor emulation as full MPC control for all old Lämpöässä systems. Use an *actuator-capability model*: weather-curve-only vs partial compressor response. Validate against measured electricity and tank temperatures.

## Preferred control modes: check in this order

1. **Ouman optional MODBUS-200 / EH-485 adapter**: manual says EH-201/L supports optional bus cards; determine whether the specific unit has one and what values it allows controlling before making claims.
2. **Ouman dry-contact night setback / Kotona–poissa**: the manual describes a configurable digital input. **Important:** the same input may instead be configured as `Maalämpöohjaus` or alarm and may be critical for startup/automatic operation. Confirm configuration before any connection, and never repurpose a critical input.
3. **TMO outdoor sensor emulation**: only if model-specific electrical checks, system-level safety assessment, physical fail-back to original sensor and response testing pass.
4. If none is safe: **read-only/advisory**, no automatic manipulation.

## Required Lämpöässä-specific tests

- Obtain exact nameplate, year, controller hardware/version, electrical schematic, present configuration and actual sensor type.
- Measure independent true outdoor temperature, mixed supply/return, tank top/bottom and compressor electrical activity (external metering where appropriate).
- Independently verify Ouman curve vs real supply feedback and heater/tank start/stop, including external night setback, room compensation, min/max supply, summer cutoff and other limits.
- Verify backup resistor / electric heater activation thresholds and domestic hot-water service separately.
- Demonstrate de-energized fail-back to real TMO sensor, startup shadow, loss of real temperature/clock/network, invalid readings, watchdog, stuck relay and controller sensor fault behavior on bench hardware first.
- Compare optimiser result using the **actual achievable actions** against an unchanged Ouman baseline, not a fictional ideal heat-power controller.

**Decision gate:** Candidate only. No product support claim and no wiring to a live Lämpöässä until actual controller, sensor and safety checks are complete.
