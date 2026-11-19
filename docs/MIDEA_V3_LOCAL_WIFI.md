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
  transport, designed for future integration with the one-pump dashboard.
- `tests/midea_v3_protocol_tests.cpp`: host vectors generated against the
  independently installed msmart-ng 2026.8.1 and tamper/invalid-input cases.

**State now:** native codec and read-only transport implemented; the
ESP32 firmware does not yet call this transport, enroll a pump or expose it
as a selectable/armed device. Existing Mitsubishi CN105 automation, physical
setpoints and control authorization remain entirely unchanged.

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

1. Connect read-only V3 status polling to a separately persisted per-device
   configuration and the common manufacturer-neutral telemetry API. Keep
   V3 token/key and device ID secret; do not return secrets in GET endpoints,
   browser logs or GitHub. Preserve all existing Wi-Fi, OTA and CN105 settings.
2. Identify the V3 module using a device-ID match and authenticated state
   read after IP changes. Restrict discovery to the selected local LAN and
   verify input and replay/error behavior. Never select a device solely by IP.
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