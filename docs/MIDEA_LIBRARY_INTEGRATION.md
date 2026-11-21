# Midea library integration model (2026-11-21)

## What the existing integrations do

- **Home Assistant Midea (Core since 2026.8):** the integration exposes Home Assistant climate/sensor/switch entities while the Python `midea-local` library performs LAN discovery, model capabilities, protocol framing and updates. Home Assistant is the frontend, not an ESP32 runtime.
- **msmart-ng:** a separate Python Midea AC LAN stack; its V2/V3 handshake, C0 state parser and 0x40 full-state encoder are already used as interoperability references by Enevina Lite. The Core-side Frezzer Nordic Midea V3 pilot uses this stack.
- **midea-local:** multi-appliance, not AC-only. The upstream package in `third_party/midea-local/midealocal/` contains V2/V3 LAN transport, discovery, cryptography, capability queries, AC commands and model-specific exception tables, plus other appliance protocols. Upstream tests are preserved verbatim.
- **MideaUART / ESPHome Midea:** C++ UART path for a *wired* adapter that replaces or bridges the Wi-Fi dongle, not a drop-in replacement for the existing Wi-Fi-based LAN connection.

## Exact division of responsibilities

| Layer | Location | Does it run on ESP32-S3? |
| --- | --- | --- |
| Upstream reference Python + tests | `third_party/midea-local/` | **No** |
| Midea V2/V3 LAN wire decoder, authenticated read, manual command | `pump/midea_v3_protocol.*`, `firmware/cottage/src/cottage_midea*.cpp` | **Yes** |
| Safety and authorization gates | Liten existing supervisor / manual pilot | **Yes** |
| Optimizer action dispatch into the Midea LAN adapter | Not implemented for Midea | **No** |
| Lua model-definition corpus | Separate upstream repo `midea-lan/midea-lua-lib`, pinned as documented in `UPSTREAM.md` | **No** |

## Feature parity tracking

| Capability | Upstream midea-local | Enevina Lite native C++ |
| --- | --- | --- |
| UDP LAN discovery | Yes | Yes |
| V2 LAN framing | Yes | Yes (tested in host vectors; no V2 field proof) |
| V3 token/key handshake | Yes | Yes, physically verified on this V3 device |
| C0 status / temperatures | Yes | Yes, physically verified |
| Midea manual setpoint / mode / fan / swing / power | Yes, by device-supported commands | 0x40 encoder present; writes require known 8 °C preset bit |
| B5 model capabilities | Yes | Not integrated into native runtime capability model |
| Optional B0/B1 property commands | Yes, where supported | Not generally ported |
| Multi-appliance device types | Yes | Not ported: Lite currently targets HVAC |
| Automatic optimizer-to-Midea dispatch | Integration-dependent | Not yet connected |
| Model-by-model compatibility | Library exceptions and Lua profiles | Not independently certified in Lite |

## Critical finding — short C0 cannot prove 8 °C preset state

A real V3 unit replied to the authenticated ordinary C0 query with a **31-byte frame** and 19-byte payload. The optional byte-21 8 °C preset flag is omitted. Read-only A0 short, A0 long and B1 property 0x0213 queries did not return usable state. B5 capability support is **not** proof of the *active* value of the preset. Even with a visible 22 °C target, a general full-state 0x40 write could serialize a guessed zero. PR #67 therefore conservatively rejects such writes and refuses to mark an abbreviated post-write state as proof of preservation.

The immediate feature work is a **model-verified alternate state query** or a **proven field-only setpoint control command** for the actual device, followed by host vectors and controlled real-device readback. Copying the upstream Python source gives a reference for that development; it does not make the unimplemented embedded features magically available. Do not fall back to cloud control or silently enable writes.

## Source and licensing

Vendored midea-local: `425bba4a05b74b619d31bf2d8b5a6baf030aa042`, Python package version `15.2.1`, MIT license. See `third_party/midea-local/LICENSE`, `third_party/midea-local/UPSTREAM.md` and `THIRD_PARTY_NOTICES.md`. Python source is development-only and excluded from firmware builds. Lua/plugin dependencies are external submodule projects, not included in this snapshot.
