# System audit — 2026-11-24

Scope: Lite common controller, CN105 gateway and direct serial, ECHONET,
Gree, Midea V2/V3, Daikin, MHI WF-RAC, settings and persistence, MPC and
identification, web UI, OTA and build/test coverage. Integration baseline:
PR #112 (49d0d99) plus merged CN105 defrost handling (#110, 9a47bab).
This is a source, host-test and firmware-build audit, not device certification.

## Findings and changes

* P1 resource risk: inspection of Xtensa stack frames found especially large
  Gree worker and web handler frames. The Gree bounded datagram buffers,
  settings form and Midea discovery response buffers now use checked
  request-local allocations, and MPC response formatting uses PSRAM. No
  new watchdog, reboot loop, or task is introduced. Allocation failure is
  handled without proceeding with a settings change or sending an invalid
  response. Field stack high-water and reset diagnosis remain open.
* P2 uncertain automatic command: a failed or ambiguous write could be
  repeated on later optimization ticks. The shared reconciliation policy
  prevents further automatic dispatch until a fresh matching device state
  is observed or a new explicit control authorization is granted. Matching
  readback is not a claim of a causally proven write acknowledgement.
  Manual operation and ordinary read-only polling remain available.
* P2 Gree freshness and readback: a failed preflight must invalidate stale
  observations; setpoint-only commands must preserve power, mode and frost
  state. An external change clears old confirmation evidence. The same
  principle is applied to the common command and learning paths.
* P2 model identification: uncertain control is explicitly represented in
  the common pump state and excluded from learning and MPC eligibility.
* P2 test coverage: missing native libraries could silently omit Gree and
  Daikin DSIoT tests. The required-protocol-tests build option fails early
  when dependencies are absent. CI should use this option and install the
  native dependencies.
* CN105 defrost handling was independently reviewed and merged in #110.

## Verification and remaining limits

The audit includes Debug and Release native suites with cJSON and OpenSSL,
Python source and integration tests, JavaScript UI tests, and an ESP32-S3
firmware build. Tests exercise uncertain and late readback, authorization,
freshness and command reconciliation. See the PR review for exact results
and tested revision.

Real-device compatibility, network fault behavior and stack high-water
measurements after the changes require separate field tests. Compilation
does not demonstrate absence of runtime resets. A previously reported
watchdog reset is not localized to a specific task by this audit. No
production device was flashed or given a new control command during the
audit. Automatic cooling optimization remains outside the implemented
heating optimizer. All changes require review and CI before merge.
