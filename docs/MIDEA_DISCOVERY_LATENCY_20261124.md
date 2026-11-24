# Nonblocking Midea LAN discovery

Follow-up to the system HTTP and storage audit (#112–#115).

## Finding

The Midea discovery POST handler performed a synchronous 1350 ms UDP
receive loop, with a 100 ms shared network-budget wait and heap-backed
WiFiUDP packet buffers. Even a completely healthy discovery held up the
single dashboard HTTP task. Resource contention could instead return
NETWORK_BUSY before attempting discovery. The code is read-only, but
latency and memory pressure affect every other dashboard route.

## Repair

The same authorized POST endpoint starts an owner-requested job and
returns HTTP 202 with its scan ID. Polls require the existing token and
the exact scan ID, and do not enqueue new work. The existing Arduino
loop advances the job in short steps: checked zero-wait admission to
the existing network budget, station-bound UDP socket, four bounded
nonblocking datagrams at most per turn, and no receive delay loop.
There is no additional FreeRTOS task, lock, watchdog or login.

Both standard discovery ports retain the global and directed broadcast
attempts. At least one successful send per port is required. Maximum
queue wait is 5500 ms, receive window is 1350 ms and results expire
after 30 seconds. A substantial receive gap, changed link/endpoint,
OTA maintenance, failed socket or network admission failure is reported
as failure rather than a successful empty shortlist. No stale scan
result is returned after a network change. The result JSON allocation
uses checked PSRAM. Existing validated device metadata, duplicate
handling, unsupported-device visibility and owner selection are
preserved. Discovery never authorizes control, enrolls a pump or
writes NVS.

The browser polls the same scan ID, with at most 32 attempts and 300 ms
spacing. It does not auto-save or arm. The wire contract changes from
one synchronous 200 response to 202 plus polling; legacy external
clients must handle the new pending response. The bundled UI is
updated in the same image.

## Verification boundary

The native harness compiles the actual firmware route and discovery
step with controlled socket/clock/memory-budget boundaries and the
production packet decoder. It covers no network work in the request,
packet quota, typed and legacy replies, duplicate IDs, memory
contention, cancelled links, OTA maintenance, failed sends, invalid
scan IDs and closure of sockets. Browser tests cover pending polls,
scan-ID mismatch, typed and legacy selections. This is not proof of
interoperability with every Midea appliance. The target build and
physical evidence are reported separately. No new physical pump
commands or production flash were requested for this repair.
