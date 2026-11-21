# Midea AC model discovery and native controls

> **For agentic workers:** use the executing-plans / subagent-driven-development workflow, with protocol and UI work in separate files. This plan records the user's implementation request; work proceeds in this session.

**Goal:** Discover the connected Midea AC's local protocol, capabilities and controls using the same pinned protocol reference as Home Assistant, without a brand whitelist or a Python/HA runtime on ESP32.

**Architecture:** Keep V2/V3 LAN transport separate from the AC message family. A volatile per-device profile accumulates validated appliance metadata and B5 capabilities; fresh A0/C0/BB responses supply state. Native command selection and both API/UI validation consume that profile. Auxiliary/capability replies never become state-freshness evidence.

**Tech stack:** C++17, ESP32-S3 Arduino/PlatformIO, native OpenSSL protocol tests, Python source checks, Node dashboard tests. Reference: vendored MIT midea-local 15.2.1 and cited MIT msmart-ng command semantics.

**Spec:** The user's request in this conversation: implement broad Midea model support and automatically select the correct controls like the Home Assistant project. This is the AC/air-to-air adapter in the existing one-heat-pump Lite product. Other Midea appliance device types are not AC variants and are not advertised as implemented by this change.

## Global constraints

- Native ESP32 operation; no cloud/HA/Python runtime dependency.
- Use the existing Wi-Fi enrollment, selected-device and manual-control workflow.
- Preserve explicit V2/V3 selection, authenticated V3 transport, command deadline and independent readback.
- Never turn a capability flag into a claim about the active value of a setting.
- Do not silently clear an unknown 8-degree preset with a whole-state command; selective property commands do not touch that field.
- Keep capability negotiation bounded and outside the timed write pre-read; still acquire live auxiliary fields required to preserve and confirm the requested command.
- Bind profile/state/queued command evidence to device, configuration, selected-adapter and Wi-Fi identity.
- No physical flashing or pump commands are part of software build verification.
- Source/public fixture data only; no local identifiers, credentials or raw device logs in commits.

## Review focus

1. Valid capability metadata with no fresh basic state must not enable or confirm control.
2. Missing B5 tags differ from explicit unsupported tags; additional pages must not erase earlier reports.
3. Short, truncated, unrelated or checksummed-but-malformed bodies must not fill absent fields with a false active-state claim.
4. Mode changes must use the destination mode's range and temperature step; UI and API must agree.
5. A link/adapter/configuration change during a read or command must invalidate its result and prevent the final SET.

## Tasks

- [x] Read main and PR67; preserve the original 15-second command deadline and reviewed short-C0 fix in the feature branch. Baseline: 51/51 native targets, 21/21 Midea Python tests.
- [x] Implement strict B5 decoding, multi-page capability merge, per-mode limits and semantic mode/fan/swing support. Test real wire vectors, every documented value set, malformed input and unknown/false separation.
- [x] Implement appliance metadata, A0/C0/BB basic and auxiliary state, selective B0/B1 properties, and family-aware command/readback selection. Test upstream-derived vectors and negative preservation cases.
- [x] Integrate bounded read-only negotiation and profile caching in both V2 and V3, with no authentication downgrade or SET retry. Test timing, identity and freshness behavior.
- [x] Expose capabilities/state separately through the API; adapt dashboard controls and validate drafts/server commands. Test dynamic ranges, unsupported selections, preserved drafts and disconnect behavior.
- [x] Run native Debug/Release, Python, real dashboard JavaScript and ESP32-S3 build; inspect the complete diff with an independent reviewer and resolve material findings.
- [ ] Complete the repository-required GitHub Codex review before merge; the published PR remains a draft pending this separate gate.

## Evidence and decisions

- Home Assistant's current Midea integration pins midea-local 15.2.1. Its official documentation itself describes partial support for some newer API-v2 devices; "all models" is a design aim, not a field certification.
- The upstream classic writer defaults absent frost state to false. Lite keeps unknown separate and supports explicit user-requested preset changes or a reported unsupported feature; it does not claim those defaults preserve an unobservable setting.
- Model-specific layouts are applied only from actual matching identity or a validated protocol response. Unknown models use the generic supported families and capability negotiation.
- The feature branch includes PR67 to avoid leaving its tested deadline fixes behind; no separate automatic optimization activation is implied.


## Review findings and regression evidence

Focused and independent whole-branch reviews found concrete issues and drove
behavioral regressions in the production transport/codec and dashboard:

- SET echoes cannot satisfy independent QUERY readback.
- C0→A0 keeps only same-acquisition fields; B1 properties are acquired afterward.
  Complementary short tails retain measured C0 frost when A0 omits it; a later
  measured A0 value takes precedence over earlier C0, and capability inference
  never overwrites a measured bit.
- BB timer writes acquire current SN8 in pre-read and independent readback.
- Send time counts against the exchange/acquisition budget.
- The exact 22013279 B5/notify2 temperature response is correlated to a requested 007e tag.
- Mode availability is independent of the previous mode's target; a destination-valid
  target is visible in the UI and required in the submitted patch when needed.
- Same-device configuration re-enrollment resets bounded metadata attempts;
  configuration mutation invalidates in-flight/queued command evidence.
- A0-only observations and missing C0 humidity cannot invent whole-state values.
- An active C0 bit with conflicting Smart Eye / independent-auxiliary semantics
  cannot be encoded by guessing both meanings.
- Absolute display control needs live B1 evidence, not only generic B5 support.
- Preset transitions follow the reference's exclusive modes and Dry exit behavior.
- Inferred unsupported/off frost evidence is kept distinct from observed live state.

Final verification: 57/57 native Debug and 57/57 Release targets; 22/22 Midea
Python tests; 5/5 translation checks; both dashboard JavaScript suites; ESP32-S3
build/link and release-hygiene scan. The PR records the exact tested tree and
firmware resource sizes. Independent in-session review closed all substantive
findings and marked the source ready for GitHub review.

The full Python discovery ran 238 tests with five failures and one
skip. All five failures were reproduced unchanged in an isolated `git archive`
of main `0b005dc7925c3e29cd7d97fe9442102bc9ae98eb`, then compared with the working tree:

- `test_cn105_http_source.test_two_bounded_read_attempts_only`
- `test_cn105_sse_visibility.test_physical_commands_are_pilot_armed_not_source_gate_bypasses`
- `test_ota_signing.test_signed_pull_worker_must_start_before_pending_image_commit`
- `test_premium_dashboard_source.test_read_only_market_endpoint`
- `test_premium_dashboard_source.test_ui_has_no_external_assets_or_mock_sensor_numbers`

These unrelated source-string assertions were not changed by this task. No
physical flash, pump command, automatic-control activation or firmware release
is claimed by host/build verification. Repository-required GitHub Codex review
remains a separate merge gate from the in-session independent review.
