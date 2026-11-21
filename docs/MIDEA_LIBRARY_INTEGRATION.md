# Midea model discovery and native controls (2026-11-21)

Enevina Lite discovers Midea **AC device type `0xAC`** metadata, message family
and reported capabilities, then uses that profile to select native ESP32
queries, commands and dashboard controls. A brand or retail-model whitelist is
not required for the generic classic and BB AC families.

The reference is the vendored **midea-local 15.2.1**, also pinned by
[Home Assistant](https://github.com/home-assistant/core/blob/dev/homeassistant/components/midea/manifest.json)
when checked on 21 November 2026. Python and Home Assistant do not run on the
ESP32. The implementation is C++ using the existing local V2/V3 connection.

## Implemented coverage

| Function | Native behavior | Evidence / limit |
| --- | --- | --- |
| Local discovery | Existing owner-triggered shortlist; bounded metadata discovery for the selected device | Validated packet length, signature, device ID, source IP, model, subtype and advertised TCP port |
| LAN transport | Explicit V2 or authenticated V3 | Existing independent vectors; earlier V3 telemetry field evidence. New features are host/build tested |
| Message family | Appliance protocol-version query; classic C0/A0 and encapsulated BB selection | Alternate queries are read-only; failed V3 authentication never triggers V2 fallback |
| Capabilities | Primary and additional B5 pages, merged without erasing prior known fields | Unknown, unsupported and supported remain distinct |
| Core control | Power, mode, target, fan; classic-family swing | Destination-mode limits, fan/swing values and whole/half-degree resolution validated |
| General presets | Eco, Turbo, Sleep, freeze-protection preset and Anion where implemented | Support does not imply a current on/off value |
| B0 properties | Absolute display, vertical/horizontal angles, rate select, Breezeless, Indirect Wind, Self-clean and Sound | B1 evidence/readback; property-only commands do not serialize the whole-state frost flag |
| BB family | BB/11 basic state, BB/10 indoor/SN8, BB/30 outdoor, BB/20 commands | Current timer/SN8 evidence required for timer-preserving writes; no generic BB swing/freeze/anion claim |
| Model `22013279` | Exact identity selects `0x007e` temperatures in B1 or B5/notify2 | C0 temperatures are invalid for this model until current-cycle `0x007e` arrives |
| Dashboard/API | Dynamic choices, limits, steps, optional controls and separate state-known information | Native validation also enforces API requests; valid drafts survive refresh |

A capability page, appliance reply, optional property or SET echo cannot supply
basic-state freshness or confirm a command. The write path obtains a fresh
pre-read, sends one SET, then performs an independent QUERY/readback. A lost ACK
does not cause another SET.

The reference's mutually exclusive Eco/Turbo/Sleep/Freeze presets cannot be
enabled together. Selecting one clears incompatible presets; confirmation checks
the intended resulting state. Leaving Dry selects Auto fan unless an explicit
supported fan is requested.

A mode change uses the destination mode's range. When the current target does
not fit, the dashboard proposes a visible valid target and sends mode and target
together. An invalid mode-only API request remains rejected.

## Identity, defaults and bounded negotiation

The volatile profile is bound to the selected device, address, transport and
configuration/link generations. Changing identity resets discovery/capability
attempts. Credentials and active readings are not retained in that cache.

Metadata discovery is attempted at most twice per identity. The appliance and
two B5 probes each have at most two attempts, with at most two pending probes
per normal acquisition. Basic state and its required A0/B1/BB supplements are
read before optional appliance/B5 negotiation. Some modules queue unsupported
queries, so putting them first can delay a valid C0 until a different query is
already active. Optional probes use only the remaining acquisition budget;
strict response matching and the 9.5-second total limit are unchanged.
Timed write pre-reads use the profile without repeating
capability negotiation, but still acquire required live fields such as BB
SN8/timer or selective property state.

Silent optional queries never become negative support flags. If B5 reports no
temperature limits, the generic fallback is 16–30 °C with 1 °C steps; the API
identifies these as defaults. Reported ranges replace them. Commands and the UI
also obey the existing approved pilot range.

Changing the pump/configuration or losing the link invalidates old observations
and queued-command evidence. The original command deadline is checked again
immediately before SET, after connection and authentication latency.

## Short C0 and preserving other settings

A short C0 may omit target humidity, the optional 8 °C heating preset and comfort
mode. Requiring all of them before any ordinary command blocked working legacy
devices. Lite now follows the existing msmart command format and its explicit
`AirConditioner.apply()` defaults: absent target humidity is encoded as 40 and
absent optional boolean fields as false. midea-local also defaults unreported
frost/comfort flags off. These are compatibility defaults for an outgoing
whole-state command, not proof that an unreported setting was already off.

The command-only copy never changes the observed state, `*_known` flags or
reported capabilities. Current observed values and explicit supported patches
take precedence over defaults. A0 is still tried as a supplement; when it
complements C0, only fields from that same acquisition are retained. Previous
sessions do not fill gaps.

Independent readback must confirm the requested change, basic power/mode/target/
fan/swing and other observed settings. If a previously observed optional field
disappears or disagrees, confirmation fails. A field absent from both reads does
not block confirmation of the basic command. If it first appears after SET, it
must match the encoded value. An explicit frost-preset change still needs a live
matching frost observation. Unreported settings cannot be claimed preserved.

Synthetic 19-byte C0 tests compare the outgoing bytes with the executed pinned
msmart serializer, then verify a separate short-C0 readback. Authenticated V3
and V2 transport tests cover exactly one SET with unknown fields remaining
unknown. These tests are separate from the physical device test record.

Selective B0 commands remain independent because they do not
serialize the whole-state fields. B5 display support alone does not
prove support for the alternate B0 absolute-display command: that writer needs
a current B1/0017 observation. The legacy display-toggle command is not exposed
as a writable fallback. An active C0 bit with conflicting Smart Eye and
independent-auxiliary-heat meanings also blocks general encoding until its
layout can be established; property-only controls remain independent.

References: [msmart apply()](https://github.com/mill1000/midea-msmart/blob/dd0994755bb078b613837038f2d67d97fab133ab/msmart/device/AC/device.py)
and the vendored midea-local `MessageGeneralSet` / `make_message_set`.
Source/host results do not establish compatibility with every retail model.

## Code map

| Layer | Files |
| --- | --- |
| V2/V3 framing and cryptography | `pump/midea_v3_protocol.*` |
| Capability semantics and limits | `pump/midea_ac_capabilities.*` |
| AC state, commands and confirmation | `pump/midea_ac_protocol.*` |
| Discovery metadata | `pump/midea_discovery_info.*` |
| Identity, probe plan and deadline arithmetic | `pump/midea_read_plan.hpp` |
| ESP32 transport and acquisitions | `firmware/cottage/src/cottage_midea.cpp` |
| Provisioning, API, pilot and queue | `firmware/cottage/src/cottage_midea_service.cpp` |
| Finnish/English dashboard | `firmware/cottage/include/cottage_premium_dashboard.hpp` |
| Unmodified upstream development reference | `third_party/midea-local/` |

## Limits

- Coverage is **AC/air-to-air over V2/V3**. V1, newer API-v2 devices, hydronic
  C3/CD and other non-AC appliances require separate adapters. A discovered
  non-AC type is not sent AC queries.
- The listed controls are implemented, not every upstream entity. Fresh-air
  accessory commands, energy counters and arbitrary Lua profiles are not
  exposed as implemented commands.
- Generic protocol coverage is not model-by-model physical certification.
- Captured BB vectors verify positive temperatures. Negative BB readings use
  signed little-endian hundredths with plausibility checks; no captured
  negative-temperature vector has independently verified that interpretation.
- Initial V3 token/key provisioning stays separate. No cloud dependency or new
  settings-storage format is added.
- Automatic optimizer-to-Midea dispatch is not connected by this change.

[Home Assistant's documentation](https://www.home-assistant.io/integrations/midea/)
also makes feature availability depend on type, model and reported capabilities,
and notes partial support for newer API-v2 devices. All physical models must
not be described as tested based on either a source copy or host vectors.

## Verification and attribution

Native tests cover B5 semantics, malformed inputs, C0/A0/BB and B0 wire vectors,
model-specific temperatures, deadlines and identity changes. The transport test
compiles the actual firmware transport against a deterministic fake LAN with
production framing, cryptography and parsers. Node tests execute the dashboard
JavaScript; a Python test compiles and executes the actual service policy with
the codec. Debug/Release and the ESP32-S3 build are separate checks, not field
proof.

References:

- [midea-local 15.2.1](https://github.com/midea-lan/midea-local/tree/425bba4a05b74b619d31bf2d8b5a6baf030aa042),
  MIT; full source/tests/license retained under `third_party/midea-local/`.
- [msmart-ng 2026.11.0 AC commands](https://github.com/mill1000/midea-msmart/blob/dd0994755bb078b613837038f2d67d97fab133ab/msmart/device/AC/command.py),
  MIT, supplementing the earlier 2026.8.1 transport vectors.

See `THIRD_PARTY_NOTICES.md`. Python, Lua/plugin submodules and Home Assistant
are not linked into firmware.
