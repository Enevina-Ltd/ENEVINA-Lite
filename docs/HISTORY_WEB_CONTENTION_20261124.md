# History and flash contention in the web loop

Follow-up to the HTTP ingress/response fixes (#113/#114).

## Failure mechanism

The history worker holds its existing mutex throughout LittleFS flush,
compaction, and optional OTA history acknowledgement. Model checkpoint
file operations use the same mutex. The history HTTP endpoint and the
optional OTA history lookup in /api/status previously acquired it with
portMAX_DELAY. Therefore a slow storage operation could stall the
Arduino web loop before its bounded response transport had any chance
to act. This is a source-confirmed blocking path, not proof of the
historical physical watchdog reset's origin.

## Repair

- Both optional web reads now try the existing history mutex with zero
  wait. The history endpoint returns HTTP 503 HISTORY_BUSY and
  Retry-After: 1 when flash work owns the mutex. Optional update evidence
  remains unknown on a contended lookup, not a fabricated absent event.
  The independent durable OTA summary remains the first choice.
- Successful responses retain the same snapshot, data provenance,
  persistence flag and output. The storage worker, persistence format,
  write ordering and verified OTA acknowledgement remain unchanged.
- The query points, event copy, summary and response chunk live together
  in one checked request-local PSRAM allocation, automatically released
  on every return path. No internal-heap fallback.
- The dashboard distinguishes a read failure from empty history. Responses
  are tied to the request sequence and selected range, so a late older
  response cannot overwrite a newer history selection. Existing periodic
  refresh and manual range selection recover normally.
- /api/status exposes history_read_busy. No new tasks, locks, watchdogs,
  settings, reboot behavior or physical write permissions.

The host harness compiles the real history reader functions with the
production history query implementation and a controlled mutex/HTTP
boundary. It exercises contention, immediate 503, no accidental unlock,
cleanup, successful later read, correct record and persistence fields,
and PSRAM allocation failure. The browser-side helper harness tests
out-of-order completion, failed reads and recovery. These are not
measurements of target flash latency or ESP32 stack high-water marks.

Remaining: target stability under simultaneous storage, dashboard,
OTA and sensor work, and other synchronous handlers such as the bounded
Midea discovery pass and air-history NVS checkpoint. Historical reset
origin is still unproven; no field flash or physical pump command was
performed during this source repair.
