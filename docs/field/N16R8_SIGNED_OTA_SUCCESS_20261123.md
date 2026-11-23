# Signed OTA end-to-end: real ESP32-S3 N16R8, 23 November 2026

**Scope:** one development test board, not approval for every pump adapter.
ESP32-S3 rev. 0.2, 16 MiB flash, 8 MiB OPI PSRAM connected to Core over USB
and local LAN. A Midea V3 unit was selected. No CN105 bridge, Shelly meter
or indoor-bound RuuviTag. Automatic heating control remained disarmed:
**no heating commands were sent**.

## Safety and recoverability

The whole 16 MiB test flash was backed up locally before trials (A/B images,
NVS and the OTA selector). Failed attempts were staged through app0; after
each aborted image transfer, the originally confirmed app1 contents were
restored from backup over USB and verified by esptool flash-image SHA.
No signing, TLS, hash, boot validation, or rollback safeguards were removed.
The local HTTPS distribution service allowlisted only the identified test
board and had its previous restricted-client configuration restored after
the test. No private signing keys entered source control.

## Physically observed failures before repair

- Image-lease admission originally required 76 KiB total internal memory
  and a 20 KiB contiguous block. Status 3 did not distinguish the reason.
- With bounded image-lease retries a signed image reached 163,840 bytes,
  then aborted with **22,484 bytes free** versus the unchanged
  **24,576-byte** in-flight memory floor.
- The read-only HTTPS server originally retained a five-second socket
  timeout for the entire transfer while the ESP32 wrote 2 KiB chunks
  with scheduler delays. Extending only the streaming socket timeout
  to 45 seconds (handshake still five seconds) extended a test to 1 MiB.
- Stack memory optimization then reached 1,804,288 bytes before a transient
  24-byte violation: **24,552 bytes free** versus 24,576 required.

These were measurements from the physical test card.

## Implemented fix

- Read-only phase-specific diagnostics for manifest admission, NVS, image
  lease and transfer, distinguishing internal-memory refusals, mutex, Wi-Fi,
  HTTPS reads, flash writes, integrity checks and boot selection.
- Bounded maximum 12 attempts separated by 400 ms for image-lease
  acquisition while network readers are paused. Admission still requires
  76 KiB total internal / 20 KiB contiguous memory.
- During downloading, at most eight 40-ms rechecks for a temporary memory
  dip. **No HTTPS reads or flash writes occur below the original 24 KiB
  in-flight minimum.** Persistent memory shortages still abort the image.
- Measured FreeRTOS stack headroom permitted 8 KiB Midea (previously 10 KiB),
  5 KiB BLE (previously 6 KiB), and 6 KiB MET weather task under the Lite
  shared-memory guard (previously 7 KiB). Other weather users keep 7 KiB.
  Optional CN105 and Shelly task deferral from PR #98 remains in place.
- The HTTPS server retains the original TLS root verification, strict client
  IP allowlist and five-second TLS handshake timeout; the per-I/O streaming
  timeout is now 45 seconds for a slow ESP32 flash writer.

## PHYSICAL SUCCESS

A P-256 signed local-lab manifest authorized version **2026112306**
(2,532,928 image bytes) for the matching test board. The device verified
its signed manifest/channel, the downloaded image SHA-256 and the ESP-IDF
application, then flashed the inactive bank and booted into app1.

1. app1 entered PENDING_VERIFY with ota_image_state equal to 1.
2. Boot health checks completed, app1 was marked VALID
   (ota_image_state equal to 2; ota_boot_confirmed true on the confirming boot).
3. The durable OTA good version advanced to 2026112306.
4. A separate USB-driven hardware reboot retained app1 VALID and the
   durable good version 2026112306. Later ordinary boots do not re-set the
   one-shot ota_boot_confirmed flag.
5. Midea V3 was still selected and fresh; settings, country FI and room
   source choice survived; prices and MET weather were valid. Automation
   remained disabled.
6. After testing, the local OTA service and signed release distribution
   were restored to their previous restricted state. Used test versions
   remain archived and cannot be reused.

This is end-to-end real-hardware evidence for the original issue #47
image-start and transient-memory failure mode in the N16R8/Midea setup.

## Outstanding release gates

- The original CN105/Ruuvi/Shelly field configuration has not yet undergone
  this complete physical OTA path. That workload can have different RAM
  consumption; preserve the safety guards and verify it separately.
- Blank-board installation, other adapters and unattended automatic heating
  need their own acceptance tests.
- Last-update history may lag the NVS-confirmed good version until the
  update event is durably flushed. Never treat the history card alone as
  evidence that signed OTA succeeded.

The public document contains no LAN addresses, device identity, passwords,
tokens, private certificates or publisher secrets.
