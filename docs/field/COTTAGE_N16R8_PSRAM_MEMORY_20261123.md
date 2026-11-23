# Cottage ESP32-S3 N16R8: signed-OTA-preserving USB update and PSRAM recovery

**Field bench date:** 23 November 2026. Evidence is from the owner's
physically connected cottage Lite controller; not a simulated ESP32.

## Device identity and recovery

The actual cottage controller was identified through its CH340 and later its
native Espressif USB/JTAG interface. The chipset matched ESP32-S3 rev 0.2,
16 MiB flash and 8 MiB PSRAM. The entire 16 MiB flash was captured in a
private directory by retrying 512 KiB reads after intermittent USB serial
errors. The backup passed exact-size checks and was checked against
independent bootloader/partition reads. No private flash contents are
committed to this repository.

Original A/B boot state:
- app0: sequence 7, VALID, retained unchanged as a rollback image
- app1: sequence 8, ABORTED after an earlier attempt

The freshly built local signed-pull-capable application was written only to
app1. Its public OTA channel marker matched the old app0 image exactly. The
new app1 image was flash-verified using esptool, and selecting app1 altered
only one state byte in the validated original otadata sector. The bootloader
subsequently reported loading app1 (offset 0x650000), and the application
reported VALID. NVS, site settings, Wi-Fi, pump adapter, RuuviTag binding,
Shelly configuration and the existing LittleFS history were not erased.

The application image is a **USB update**, not a newly signed-pull
transaction: the persisted last-good signed OTA version is historical.
Do not present that NVS version as the version of the active USB-flashed
application.

## Initial problem found on this exact configuration

With Mitsubishi CN105 SSE, Shelly polling, RuuviTag BLE, market, weather,
MPC and signed OTA all enabled, the existing release had only about
**70–74 KiB free internal RAM**, insufficient for weather/price TLS admission
(78 KiB) and often also signed OTA manifest admission (72 KiB).
The original bench showed zero new MET Norway forecast points and a
price provider error, even while previously cached market slots remained.

The SSE bridge and Shelly reader themselves worked. The CN105 SSE feed
was not origin-verified as a fresh UART pump observation; this is a
separate issue and has not been bypassed.

## Root-cause repair

The optimizer created four ordinary new[] scratch arrays under the ESP32
allocator's internal-memory preference. They total roughly **15 KiB**:
200 PricePoint, 64 OutdoorPoint, 200 Spot15 and max_horizon_slots Row.
All four now use explicit PSRAM-capable allocation, with placement
construction and the existing fail-closed all-buffers-present guard.
Large MPC/identifier buffers were already in PSRAM and remain there.

No task stack moved to PSRAM. No OTA, weather, price or BLE memory floor
was lowered; no new watchdog, PIN, login, compressor control rule or
physical pump command was introduced.

## Verified on the actual cottage ESP32 after USB reflash

The exact current firmware was rebuilt for esp32s3_cottage_release,
written and flash-hash-verified in app1, and rebooted with app0 intact.

- App1 remained VALID and the controller remained explicitly disarmed.
- MET Norway forecast successfully fetched **58 points**.
- Nord Pool price provider returned **HTTP 200** with 192 stored slots.
- CN105 SSE remained connected; Shelly successful poll counter increased.
- Initial free internal heap measured **77,292 bytes**, recovering
  to around **79,880–79,724 bytes** after setup; the network budget denial
  counter stayed zero throughout the monitored observation.
- No physical HEAT/OFF/setpoint commands were sent.

## Limitations before normal unattended heating

- A connected CN105 SSE socket is not itself evidence of a successful
  UART-origin pump sample. The bridge's SSE snapshots remain labelled
  UNVERIFIED until pump-origin proof is available. This is not fixed by
  relocating optimizer data to PSRAM.
- The bound cottage RuuviTag was stale when the controller was physically
  moved away from its normal installation; nearby unbound BLE devices do
  not substitute for it.
- Hardware-specific repeated signed OTA under the *full* CN105/Shelly/BLE
  workload, control arming, real HEAT/OFF and long unattended stability have
  not been approved by this source change.

No network secrets, SSIDs, private device IDs, IP addresses or signing keys
are included in this public report.
