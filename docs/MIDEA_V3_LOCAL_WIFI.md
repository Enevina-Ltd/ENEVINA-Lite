# Midea V2 / V3 local Wi-Fi integration (experimental)

## Scope

Enevina Lite targets a *single* Midea/OEM air-to-air heat pump per ESP32-S3.
A separate Enevina Core basement pilot previously read a Midea unit using
**msmart-ng 2026.8.1** over an authenticated local V3 LAN connection. This is
evidence for that individual device and protocol, **not** proof that every
Midea-branded heat pump or Wi-Fi module uses V3.

The experimental native C++ code in this change lives in:

- `pump/midea_v3_protocol.{hpp,cpp}`: V3 AES-256-CBC/8370 handshake,
  authenticated encrypted frames, nested signed 5A5A AC query and state parser.
- `firmware/cottage/include/cottage_midea.hpp` and
  `firmware/cottage/src/cottage_midea.cpp`: ESP32-S3 TCP/6444 **status + experimental manual control**
  transport.
- `firmware/cottage/src/cottage_midea_service.cpp`: NVS-backed local device
  provisioning, serialized manual control/readback and redacted `/api/midea/status`.
- `firmware/cottage/include/cottage_premium_dashboard.hpp`: Midea setup and
  state view and manual control; pump selection never grants CN105 write permission.
- `tests/midea_v3_protocol_tests.cpp`: host vectors generated against the
  independently installed msmart-ng 2026.8.1 and tamper/invalid-input cases.

## Local discovery from the Lite dashboard

The setup page has an owner-initiated **Etsi Midea lähiverkosta** button.
It runs a bounded (1.35-second) **local UDP** search to ports 6445 and 20086,
using the standard Midea discovery packet. It verifies V2/V3 response framing
and extracts the advertised 48-bit device ID, protocol and reply IP. It does
not connect to a manufacturer service or issue a pump state/write command.

- Discovery candidates populate the **IP, protocol, and device ID** fields only
  after selecting a result; no settings are saved automatically.
- If multiple devices respond, the owner selects the correct device. A reply
  is **untrusted** until the subsequently configured V2/V3 authentication and
  an actual fresh status read succeed.
- V3 still requires previously provisioned **128-character token and
  64-character key** entered directly in the owner's trusted LAN dashboard.
  Discovery cannot supply keys or prove cloudless first provisioning.
- Access is limited to the established LAN and existing per-boot settings
  token, not the open setup AP. No new password or PIN was introduced.
- No automatic Midea control or background DHCP re-enrollment is enabled.
  A later IP change can be handled through this manual discovery again.

## Verified Lite-to-Midea V3 local telemetry (20 November 2026)

On the Core machine's connected ESP32-S3 N16R8 test board, **Enevina Lite
firmware 2026112023** successfully read a live status reply from a physical
Midea OEM test unit over authenticated TCP/6444 V3 LAN. The one Midea
found by Lite's own UDP discovery matched the Core-verified device ID.
The prior build could authenticate/decrypt but rejected the first response
and reported `protocol_failed`; the working correction follows the
independently verified msmart-ng protocol's ~1 s delay after V3 handshake
before sending the first state query. The successful authenticated response
was a C0 state frame with validated mode, power, setpoint, indoor and
outdoor temperature fields. Private test-site measurements are not published.

Verified: signed A/B OTA install and boot confirmation, fresh V3 telemetry,
retained configured credentials, existing safe-control gates, no transmitted
physical control commands, no new password. The test client's OTA publisher
access was restored to its original LAN-only allowlist afterward.

**Limits:** this proves runtime LAN telemetry using V3 credentials already
locally provisioned in Core; it does **not** prove initial cloudless
provisioning, any Midea write/control, the automatic price optimizer, or
compatibility with all Midea OEM variants. The Midea manual pilot approval
remained false, and CN105 automation was not armed. A separate physical
write experiment requires owner approval and model-specific limits.

**State now:** V2/V3 local telemetry and experimental manual control of
power, mode, setpoint, fan and swing are implemented in firmware source.
Each manual command is built from fresh pump status, sent only once and
checked with an independent readback. This has passed host protocol vectors
and firmware builds, but NOT a real Lite-to-Midea physical write test.
Automatic electricity-price optimization is NOT connected to Midea yet,
and Mitsubishi CN105 controls remain separate.

## Midea V2 local LAN (legacy modules)

This branch adds an **explicit V2** option alongside the existing V3 option
in Settings > Midea. This is not automatic downgrade or a claim that Frezzer
Nordic uses V2. The owner's already-tested Frezzer uses **V3 in Core**.

- V2 sends the same AC `0x41` status query using the
  **direct 5A5A packet** on TCP port **6444**, with AES-128-ECB/PKCS#7 for
  the nested command, the Midea fixed-key MD5 signature, and an unencrypted
  device ID in the packet header. **No V3 8370 handshake, token or key.**
- Set **Protokolla V2** and enter private IPv4 address and *device ID*.
  The V3 token/key fields are hidden and intentionally blank for V2.
- V3 remains separately selectable and retains full token/key authentication.
  Existing `midea_v3/config_v1` NVS settings are read by the new service
  and interpreted as V3; on the next edit, a versioned `config_v2` record
  is written. An up-to-date `config_v1` V3 mirror remains available for
  rollback to the previous V3 firmware; switching to V2 leaves any existing
  V3 mirror untouched because older V3 images cannot interpret V2. No
  The loader also detects changed valid V3 legacy settings when upgrading
  again after an OTA rollback: modifications to `config_v1` made by the
  older V3 firmware take precedence over stale V3 settings in `config_v2`.
  An intentionally selected V2 is never overwritten by this reconciliation.
  No silent V3-to-V2 fallback on authentication failure.
- The ESP32 permits only private RFC1918 IPv4 destinations and bounded TCP
  reads. Response outer device ID, fixed-key packet signature, AES padding,
  AC frame checksum and response fields are checked before accepting status.
- V2 is **not cryptographically authenticated per device**: the shared
  fixed key is a public protocol constant. Use only a trusted LAN; do not
  confuse a signed packet with an authenticated manufacturer endpoint.
- Experimental manual control uses the shared Midea AC 0x40 full-state
  command, following a fresh 0x41 status read. The command is transmitted
  once and confirmed with a new query. A complete control packet may alter
  native timers or undiscovered model-specific flags; this needs a physical
  test before use on a connected home heat pump.
- Automatic price optimizer to Midea commands is NOT yet wired. It must be
  validated on the actual device before being declared operational.
- There has not yet been a real Lite ↔ V2 Wi-Fi-module field test, so no
  retail/OEM model can be certified or listed as confirmed Lite-compatible.

This protocol behavior is independently cross-checked against
[msmart-ng](https://github.com/mill1000/midea-msmart), notably `_Packet`
and `Security` in its LAN implementation (MIT-licensed).

## Frezzer Nordic basement pilot: verified in Core as Midea V3

The user's **Frezzer Nordic** heat pump is the **same basement Midea unit**
previously integrated and tested in Enevina Core (not an additional untested
brand). The Enevina Core Midea LAN integration previously demonstrated:
- Local **Midea V3** authentication with `msmart-ng`, device ID and token/key.
- Working local state polling, normalized telemetry, and recovery when DHCP
  changes the pump's local IP address.
- Implemented HEAT, ON/OFF and target-temperature control paths; the earlier
  production pilot explicitly had **physical control disabled**
  (`control_enabled=false`, `dry_run=true`).

**Do not reclassify this specific tested Frezzer unit as a hypothetical V2
device.** A public Nordic K12 manual mentions the OSK102/NetHome Plus module,
and other OSK102 devices have been reported to use V2, but there is no
independently verified evidence that the user's Frezzer has that exact retail
submodel or Wi-Fi module. Its **observed Core protocol is V3**. No retail
submodel, Wi-Fi-module revision or proprietary pairing secret should be
published without separate verification.

**Important distinction:** Core LAN connection and state polling have been
physically tested. The new native **ESP32-S3 Enevina Lite V3 client** has only
passed host vectors and firmware build/tests; it has **not yet been
physically tested** against this Frezzer. Lite now contains manual command
code, but NO proven real Lite-to-Frezzer command/readback evidence. Full
automatic optimization and IP rediscovery remain unimplemented. This is a high-priority first device for the Lite pilot, but it
is not yet an approved Lite-compatible retail model.

## Authentication and local behavior

A V3 module typically needs a correct device ID, 128-hex-character token and
64-hex-character key for a LAN handshake. These are *device pairing secrets*,
not Lite user passwords. A previously paired module can communicate locally
without a cloud request for every status poll. Initial pairing or credential
retrieval may depend on the vendor's ecosystem, module firmware and network.

**Never** put secrets, MACs, private IPs, serials, or raw discovery/packet
captures from a home into this public Git repository, pull requests, issues,
test vectors or device logs. The tests use only invented device data.

The TCP reader verifies cryptographic hashes before accepting a response;
it imposes bounded packet sizes, bounded read time and restricts literal
destination addresses to private IPv4 networks. It returns only explicitly
decoded AC state fields. Packet receipt time is **not** treated as an
independently proven timestamp from the heat pump.

## Steps before actual Lite compatibility

1. The V3 client now has separate NVS credentials and redacted status.
   **Remaining:** verify device-specific telemetry and integrate authenticated
   readings with the manufacturer-neutral optimizer data path. The network
   setup endpoint uses **unencrypted local HTTP**, so credentials can be
   intercepted on an untrusted network; treat this as a developer preview,
   not a production-ready security design. NVS secrets are not yet
   encrypted at rest. Do not use on untrusted LANs or offer for public
   production deployment until enrollment and storage are secured.
2. Implement IP self-healing using the V3 device-ID match and a successful
   authenticated state query after DHCP changes. Restrict discovery to the
   selected local LAN and verify replay/error behavior. Never select a
   device solely by IP.
3. Confirm indoor temperature, operating mode, target temperature and
   supported temperature range on the specific test model. The local V3
   protocol may report values or capabilities that differ by firmware/OEM.
4. Implement *model-specific* HEAT/ON/OFF and temperature-setpoint writes
   only behind the existing Supervisor and explicit user opt-in, with
   state-preserving commands, state readback and negative regression tests.
5. Test on the real ESP32-S3 and then independently on an authorized
   Midea device, without disturbing any currently deployed Core controller.
   Only then mark a model selectable for actual control or publish a
   model-level compatibility claim.

## Technical reference / license

Wire behavior independently implemented and cross-checked using
[msmart-ng](https://github.com/mill1000/midea-msmart) (MIT license,
copyright retained by its respective authors). No Python interpreter,
msmart-ng runtime, Home Assistant or cloud API is linked into Lite firmware.
Crypto uses Mbed TLS already bundled in the ESP32 framework; host tests
use OpenSSL. See THIRD_PARTY_NOTICES.md.
## Control references and OEM coverage (19 November 2026)

- mill1000/midea-msmart (MIT) is the primary reference for V2/V3 network
  envelopes, AC 0x40 whole-state command, 0x41 status, 0xB5 capability
  queries and authentication:
  https://github.com/mill1000/midea-msmart
- wuwentao/midea_ac_lan documents many AC capabilities and OEM variants:
  https://github.com/wuwentao/midea_ac_lan
  No third-party code is copied without checking its license.
- LinzN/midea-ac provides a Java implementation for independent checks:
  https://github.com/LinzN/midea-ac
- Torsten85/midea-local (MIT) provides TypeScript transport and codec
  comparisons: https://github.com/Torsten85/midea-local
- fiam/home-assistant-midea-connect investigates Bluetooth commissioning:
  https://github.com/fiam/home-assistant-midea-connect
  V3 per-device LAN keys may still require separate cloud-assisted pairing.

The same current AC 0x40 serializer can serve devices with either Midea V2
or V3 LAN modules, as long as their exact 0xAC model and command capability
have been verified. Brand-only compatibility is never sufficient; Midea
0xCC, 0xB0/0xB1 optional properties and all additional module types require
their own implementation and testing. No blanket OEM compatibility claim.

## PR #54: Midea V2/V3 manual control review fixes (19.11.2026)

All seven initial Codex findings (five P1, two P2) have code-level
corrections pending CI and re-review. No unrelated Lite features, login,
PIN or password have been introduced.

1. Explicit, per-device pilot approval: user enters exact device ID, model
   name, and model-verified min/max setpoints. Disabled by default; not
   persisted across reboot or new device configuration. Pending proper B5
   capability discovery, model limits come from the tester, not guesses.
2. Swing constants: OFF=0, VERTICAL=12, HORIZONTAL=3, BOTH=15 (msmart-ng).
3. A full-state write is refused if the C0 frame did not contain the
   freeze-protection field. Short frames remain readable without writes.
4. Preserve independently controlled auxiliary heater flag in the C0
   state decoder and control command, then verify it in independent readback.
5. Prevent new target settings and queued old-target commands from racing:
   atomically reserve the config update before writing NVS.
6. Cancel queued commands on Wi-Fi disconnect/lost IP. Recheck the link
   generation immediately before any V2/V3 control send. No blind retry.
7. Reconcile the codec, UI, readback and pilot bounds with 12-40 C
   and enforce the smaller verified model-specific range before SET.

Tests on synthetic command vectors are not physical pump testing.
Automatic spot price control remains disconnected from this manual Midea
pilot. OEM-specific extra flags and native timers require field validation.
