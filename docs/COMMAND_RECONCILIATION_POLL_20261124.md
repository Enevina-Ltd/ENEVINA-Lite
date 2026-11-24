# Reconcile uncertain commands from independent polls

Follow-up to #112. An uncertain automatic command used to be reconciled
only inside the next automatic submission using a cached reading.
This couples uncertainty recovery to the controller asking for another
command. A HOLD or invalid planning input can then leave the observer
uncertain even after a matching fresh reading. For the CN105 ESPHome
gateway, the uncertainty was not published in the common learning state.

Split the policy into a read-only awaiting(authorization) query and a
mutating observe(authorization, fresh, mode, target) operation. Only
an independent active-device poll can resolve the pending receipt.
Wrong mode, missing/wrong setpoint, stale data and another authorization
cannot resolve it. A new owner authorization retains the existing
ability to dispatch; no new write permission is created by a read.
Midea, Gree, Daikin legacy, ECHONET, WF-RAC, serial heating adapters and
the standard CN105 gateway now reconcile on their ordinary read paths.
Draft/preflight/failed reads do not resolve uncertainty. A reconciliation
restarts request continuity, does not increment command-confirmation
counters, and never manufactures a causal acknowledgement.

For common LAN and bus learning, capture a single start state at the
quarter-hour boundary and require matching device/link/config/request
generations and the same observed heating request at the end. The
existing command queue increments a separate write-generation counter;
queued/in-flight writes are visible to learning without disabling
their own control preflight. Reject an interval with a write-generation
change, active write or unresolved command at either end, even if
the final mode and setpoint are identical. For the CN105 gateway,
retain its existing verified single-transition learning contract,
while rejecting unresolved endpoints. Reset the old POST-stage
diagnostic before an early cancellation so a stale previous attempt
cannot count as a new failed command.

No new task, mutex, watchdog, dwell threshold, password, pump adapter,
or control authorization. Tests exercise matching delayed readback,
wrong/stale mode and target, epoch changes, in-flight and intervening
writes, identity/continuity changes, and CN105 learning endpoints.
Source tests bind the transition to the actual normal polling paths.
This is code and regression evidence, not a physical command test.
The production 72-hour observation uses the already deployed release
2026112404; do not flash or change its settings for this audit.
