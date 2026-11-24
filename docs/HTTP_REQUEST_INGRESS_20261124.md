# Local web request ingress — 2026-11-24

## Source-confirmed problem

PR #113 (69b18c7) bounded outgoing dashboard responses. The pinned
Arduino-ESP32 2.0.17 SDK's incoming `WebServer::_parseRequest` still
uses blocking Stream reads for each request/header line and a fresh
`HTTP_MAX_POST_WAIT=5000` ms for every missing body segment. The SDK
can wait longer than the existing five-second Arduino loop watchdog.
Incomplete headers or bodies and a trickling client are therefore a
separate, source-confirmed reset/jam risk. This does not prove which
task caused an earlier physical watchdog reset.

## Correction

Keep the original WebServer routes, callbacks, existing local token
checks, setup-AP checks, status/content headers and outgoing response
deadline. Replace only incoming framing/admission in the existing
CottageWebServer subclass, using the pinned SDK's protected route
dispatch fields and `_handleRequest()`, without calling its blocking
parser. A single accepted connection receives at most 4096 bytes per
loop iteration via nonblocking recv. Missing data returns to the
normal firmware loop; no per-request thread, new task, watchdog,
reset or scheduler lock is added.

- A single wrap-safe 3000 ms admission deadline starts when a client is
  accepted. Partial progress cannot extend it. The outgoing 3500 ms
  response deadline starts separately after a complete request has
  been admitted.
- A checked request-local 16 KiB PSRAM allocation covers at most 8 KiB
  of headers and 8 KiB of body. No fallback to scarce internal heap.
  The request target is at most 2048 bytes, headers and decoded form
  argument count at most 64 each. Argument allocations are checked.
  Used raw receive bytes are erased before releasing the buffer.
- Existing dashboard and Wi-Fi commissioning use GET and POST with
  URL-encoded forms. Support HTTP/1.0 and HTTP/1.1, UTF-8 form data,
  percent escapes, query parameters, plain text/JSON bodies and the
  SDK's collected-header API. Reject invalid escapes, decoded NUL,
  duplicate decoded parameter names, conflicting framing, invalid
  Content-Length and body size, unsupported methods, multipart
  uploads, chunked ingress and Expect/100-continue. The local UI uses
  none of those unsupported upload/transfer mechanisms.
- Only a completely framed and validated request reaches a route.
  A timed-out/disconnected/invalid client is closed and its request
  state is released. A short rejection response is best effort,
  with no blocking write on the rejected connection.
- /api/status reports web_request_rejects and web_request_timeouts.
  Existing response abort and timing counters remain unchanged.

## Evidence and boundaries

Native codec tests exercise bytewise fragmentation, strict headers,
length overflow, transfer ambiguity, size limits, form decoding and
clock rollover. The actual ESP32 subclass is compiled into a host
socket harness with a minimal SDK boundary double. Tests use real
socket backpressure and fragmented input, test timeout during
progress, incomplete body/EOF, duplicate and invalid form fields,
allocation refusal, later valid request recovery, and existing
chunked/flash response handling. Native and firmware builds do not
prove target lwIP timing or actual browser interoperability.

There is still one HTTP client slot, now released on its bounded
admission deadline. Normal incoming request processing no longer
blocks the loop while waiting on the network. Long-running
application route handlers, flash storage and scheduling can still
consume time; they are separate audit targets. No physical ESP32
flash, pump command or production stress test is authorized by this
source fix. A hardware run should check slow/incomplete requests
alongside normal mobile dashboard loads, setup, sensors, OTA and
control, recording uptime, reset reason, heap and stack watermarks.
