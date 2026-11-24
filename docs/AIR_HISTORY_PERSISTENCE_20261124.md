# Ruuvi history checkpoint and snapshot correctness

Follow-up to #115 (history web contention) and #117 (boot memory).
The six-hour air history NVS checkpoint previously ran on the Arduino
web loop and held the Ruuvi state mutex throughout the flash write.
The HTTP air history endpoint read individual points under separate
lock acquisitions, permitting a rolling ring to mix snapshots.
A zero Preferences getBytesLength could also mean a read error,
not a proven absent key, before rebinding a legacy untagged history.

The existing history worker now calls the checkpoint after its normal
history observation, outside the web loop. Under the existing Ruuvi
guard it takes a PSRAM snapshot and marks it in flight. Flash I/O
runs with the guard released. The rebind route refuses an indoor
binding change while that snapshot can still reach NVS (503
HISTORY_BUSY, Retry-After: 1). The existing purge-before-bind and
durable-binding-before-checkpoint contract is retained. No additional
mutex, task, watchdog, authentication or physical command.

The snapshot's dirty flag is cleared at capture, not after writing;
samples received during the write (including the latest sample in
the same UTC hour) remain dirty. A failure preserves the dirty flag
and retries no sooner than 60 seconds; successful checkpoints retain
the six-hour cadence. Offline boot-relative hours are not persisted.
An actual NVS erase and commit, or explicit NVS_NOT_FOUND, is now
required for all history purges. The legacy on-flash format and
namespace/key remain unchanged.

The history endpoint copies one complete ring into checked PSRAM,
releases the guard before streaming and returns 503 on contention
instead of falsely reporting empty history. The dashboard distinguishes
failed reads from empty history, discards old asynchronous responses,
refreshes history after binding attempts, and reports temporary
HISTORY_BUSY clearly. Existing event logging is kept under its guard.
Status exposes in-flight checkpoint and failed-attempt counters.

The C++ harness extracts the actual checkpoint, purge, rebind and
history HTTP functions and runs them with the production AirHistory
and RuuviBinding implementations plus controlled NVS, clock, semaphore
and HTTP boundaries. It covers interleaved sample arrival and reading,
busy rebind, failed erase/commit/settings save, retry timing, PSRAM
failure, untrusted time, snapshot consistency and cleanup. JS tests
execute the actual dashboard functions. This does not simulate ESP-IDF
flash latency, verify NVS power-loss behavior on silicon, or eliminate
all synchronous owner-initiated settings writes and global flash
contention. Physical test results are reported separately.
