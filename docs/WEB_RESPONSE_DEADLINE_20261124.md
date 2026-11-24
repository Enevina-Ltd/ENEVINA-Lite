# Bound the local dashboard response transport (2026-11-24)

## Evidence and scope

The previous system audit (#112, merged as cbaa9dc) found a small loop-task
stack watermark and a previous task-watchdog reset. On a later read-only
field observation the same running application still reported reset reason
6, loop stack watermark 188 B and boot minimum internal heap 4,512 B.
These observations are not a backtrace tying the reset to a particular
handler, and no production request stress, flash or pump command was used.

The pinned Arduino-ESP32 2.0.17 SDK config has
`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`. Its WiFiClient::write loops up to ten
1-second select waits without progress and resets the retry count on each
partial success. WebServer::send_P (including the large flash-backed
dashboard) uses this transport; its chunked response helper also calls
WiFiClient::write directly for the CRLF after each chunk. Setting the normal
socket timeout does not bound this retry loop. Thus even without an
application deadlock or exhausted stack, a stalled receiver can hold the
existing Arduino loop watchdog's subscribed task longer than five seconds.
This is a source-confirmed failure mechanism, not a proven explanation of
the previous field reset.

## Change

Use the same WebServer routes with a derived CottageWebServer. Its two
virtual output hooks send at most 4096 bytes per nonblocking socket call.
A single monotonic 3500 ms budget begins at handleClient entry; partial
writes or multiple response chunks never reset it. Transient backpressure
yields for 5 ms; terminal errors or expiration close that connection.
All streamed JSON endpoints use the explicit bounded chunk writer, including
the final CRLF and zero-length terminator. Abort clears the chunked flag so
the inherited finalizer cannot send the missing tail. Completed responses
retain the original payload, status, headers and content length/framing.
The next request starts normally. Slow clients can receive a deliberately
incomplete response and need to retry rather than keep the controller busy.
New /api/status counters report aborted responses and longest individual
write duration. No new watchdog, task, authentication, automatic reset,
pump permission, or SDK patch.

## Tests and remaining limitations

Native tests check a full-size response, partial progress, exhausted shared
deadline, wraparound, malformed send results, exact chunk framing and
real non-reading host socket backpressure. A second native harness builds
the actual ESP32 transport source against a minimal SDK boundary double and
checks chunked success, aborted streams without a false terminator,
subsequent-request recovery, PROGMEM hook and expired handler budget.
These are host evidence, not lwIP/FreeRTOS timing measurements.

The incoming SDK parser and blocking work before the first write are not
made asynchronous by this patch. Slow or incomplete inbound HTTP headers
or bodies, and long-running handlers, remain separate audit targets.
The 3.5-second response budget is an engineering choice that leaves
approximately 1.5 seconds before the existing 5-second watchdog; scheduler
delays and SDK internals must still be measured on the actual ESP32.
Before field deployment, test fast and throttled dashboard loads, dropped
connections during each streaming API, Wi-Fi and OTA coexistence and
recovery, with pump control isolated. Capture reset reason, high-water
marks, free/largest internal heap and new response counters. Do not infer
that the previous watchdog root cause or the field heap issue is fully
resolved from source and host evidence.
