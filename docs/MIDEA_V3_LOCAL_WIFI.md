# Midea V3 local Wi-Fi integration (experimental)

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
  `firmware/cottage/src/cottage_midea.cpp`: ESP32-S3 TCP/6444 **read-only**
  transport.
- `firmware/cottage/src/cottage_midea_service.cpp`: NVS-backed local device
  provisioning, read-only polling task and redacted `/api/midea/status`.
- `firmware/cottage/include/cottage_premium_dashboard.hpp`: Midea setup and
  read-only state view; pump selection never grants CN105 write permission.
- `tests/midea_v3_protocol_tests.cpp`: host vectors generated against the
  independently installed msmart-ng 2026.8.1 and tamper/invalid-input cases.

**State now:** native codec, read-only ESP32 V3 polling, manual browser setup
and status display implemented in source and firmware build. The model has
**not** been verified with a real Lite device / Midea physical pump. Midea is
selectable for telemetry only and never enabled for physical writes; the
existing Mitsubishi CN105 control path is unchanged except for explicit
prevention of CN105 writes when another manufacturer is selected.

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
physically tested** against this Frezzer. Lite currently reads status only;
Midea heat/setpoint writes, full optimization and IP rediscovery are not yet
implemented. This is a high-priority first device for the Lite pilot, but it
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