# Boot and task-allocation memory contention

The N16R8 USB test of #116 reported an internal heap SDK minimum of
5784 bytes despite ~86 KiB free after startup. This is an SDK
aggregate low-water mark (sum of heap-region minima), not a simultaneous
free-byte sample or proof of the responsible allocation.

Source audit: forecast workers could acquire the existing memory lease
while setup still allocated permanent 6/16/10-KiB task stacks. Optional
adapter, weather, geocode and Shelly task creation could also overlap
another task's admitted TLS/BLE operation. The SDK uses internal
mbedTLS allocations. Other unguarded allocations remain possible.

Change: reserve the existing network memory mutex while creating the
boot-time tasks, initialize forecast services before history/optimizer
can consume their cache, start only configured optional readers before
release, and use zero-wait admission for later reader creation. The
existing loop retries unavailable market/weather/optional reader tasks.
Geocode and Shelly discovery report their existing busy/failure states
if admission is unavailable. Existing stack sizes, TLS/BLE thresholds,
OTA and control authorization are unchanged. No new task, watchdog,
mutex or password.

The read-only status JSON exposes boot_memory (stage, milliseconds,
current free internal 8-bit memory, SDK minimum, largest block),
worker_start_deferrals, worker_start_failures (core and adapter
creation failures) and network_sampled_min_internal. The latter is
an actual sampled free-byte minimum at existing network-budget and
successful forecast TLS handshake boundaries, not a continuous peak
memory measurement. Existing SDK minimum remains available.

Native regression compiles the real budget implementation with fake
semaphore and memory boundaries, verifying contention, lease ownership,
low-memory refusal, release, retry and sampled minimum. Source tests
enforce boot/runtime ordering. Physical cold boot and signed OTA
evidence are separate; no promise that every source of memory pressure
or NVS contention is resolved.
