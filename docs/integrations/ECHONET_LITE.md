# ECHONET Lite — cross-brand local HVAC support

Status 2026-11-24: standalone native C++17 codec and host tests; no ESP32 UDP worker, onboarding, physical writes, or field verification yet.

The target is direct local control of every compatible device, not a retail-model allowlist. One Lite controller still controls one selected physical indoor unit. Device discovery uses UDP 3610, multicast 224.0.23.0 and node-profile D6. The selected endpoint is the peer address plus the exact HVAC object instance (class 0130). Multiple objects at one IP are separate devices.

## Coverage and evidence

Community reports: [ECHONET Lite HA integration](https://github.com/scottyphillips/echonetlite_homeassistant) lists Mitsubishi Electric MAC-568IF-E, MAC-577IF-E/IF2-E, MAC-578IF2-E, MAC-587IF-E, MAC-588IF-E and certain Japanese adaptors, plus compatible Daikin and Toshiba equipment. It includes MSZ-AP, GE, GL, LN and BT, and selected ducted models. This is upstream evidence, not Lite field verification. Availability and initial enabling of ECHONET differ by module/region/firmware. Never assume a MELCloud device exposes ECHONET. Do not reconfigure manufacturer modules or cloud accounts automatically.

The codec implements format 1 Get, SetC, Get_Res, Set_Res, discovery, list/bitmap property maps (9E/9F), power, operation mode, integer Celsius setpoint, optional room/outdoor temperature, humidity, fan and fault state. It can encode HEAT, COOL, AUTO, DRY, FAN, ON/OFF and setpoint commands for a validated model; fan and other optional controls are not implemented as writes. Missing values remain absent. No fictional compressor Hz, power consumption, defrost status or pump timestamp. The code rejects truncated, oversized, duplicate or mismatched packets, failed services and invalid mandatory values. Limits (1024 bytes, 32 properties/request) are local implementation bounds. The property map proves an EPC is readable/writable, not its valid mode/temperature values; a per-model tested range and mode mask are required.

Sources: [ECHONET Lite 1.14 middleware](https://echonet.jp/wp/wp-content/uploads/pdf/General/Standard/ECHONET_lite_V1_14_en/ECHONET-Lite_Ver.1.14%2802%29_E.pdf); [device-object specification, Release Q, class 0130](https://echonet.jp/wp/wp-content/uploads/pdf/General/Standard/Release/Release_Q/Appendix_Release_Q_E.pdf); [consortium web API property mapping](https://echonet.jp/wp/wp-content/uploads/pdf/General/Standard/web_api/ECHONET_Lite_Web_API_Dev_Specs_v1.4.1_e.pdf). Independent implementation; no third-party library code copied.

## Integration work remaining

1. ESP32 UDP worker: one bounded request per endpoint at a time; explicit peer address/port, source/destination EOJ, TID and request generation/deadline checks; reject delayed/replayed responses and drain/restart session after reconnect. Support multiple discovered candidates but pair exactly one. No writes during discovery. Handle optional property SNA via bounded individual Get fallback; do not synthesize missing data.
2. Pair UI: live identity, exact instance, read/write property maps and a verified user-selected device, model-specific mode and temperature limits (separate heating/cooling limits where necessary). Store settings with backwards-compatible schema migration. Native protocol has no new Lite password/PIN; no assumed module authentication. Do not expose UDP control to WAN.
3. Integrate adapter with common PumpRoomState, Supervisor and existing worker queue. A successful Set_Res is only acceptance, not applied state. Read back all requested properties after the device has had time to apply the command; do not report success from an optimistic echo. Keep LAN receipt distinct from original pump-sensor time.
4. Preserve the heat pump's own protection and defrost. The generic HVAC optimizer needs explicit COOL versus Other and mode-appropriate constraints; do not drive cooling through the heating heuristic. Add firmware/manual control, then automatic heating and cooling, with host mocks and firmware build, before model-specific physical approval. No new watchdogs.
5. Field proof for at least one original Mitsubishi Electric module and a second manufacturer where available: discover, pair, poll, offline operation, supervised heat/cool and setpoint readback, power/network loss, recovery, model-specific capability/range checks. No new pump commands or flashes without separate approval.

## Wider compatibility priorities (not implemented by this codec)

- Mitsubishi Electric older MAC-559IF-E local HTTP /smart and /unitinfo: [MIT reference](https://github.com/Adam-Langley/ha-mitsubishi-msz). Reported on MSZ-GL60VGD. CN105 frame reuse needs transport-specific verification. Newer MAC-577IF-2E encrypted local HTTP: [MIT PyMitsubishi reference](https://github.com/pymitsubishi/pymitsubishi). Do not extrapolate to MAC-597 or other modules without evidence. Treat unencrypted, encrypted and ECHONET transports separately.
- Mitsubishi Heavy Industries WF-RAC: current Lite HTTP variant exists; HTTPS/WCBN4612L support missing. [Local protocol reference](https://github.com/blues-sechseck/Mitsubishi-WF-RAC-Integration). Preserve module-specific pairing and TLS verification; check license before copying any implementation.
- Existing Midea V2/V3, Gree LAN, Daikin legacy/DSIoT, CN105, S21, Panasonic and Haier UART: complete firmware control, model capability and field evidence rather than reimplement existing codecs. See [current compatibility](../COMPATIBILITY.md).
- Remaining native LAN candidates: Haier SmartAir2, Hisense/Fujitsu Ayla, AUX/BroadLink, Samsung local variants; distinguish cloud-dependent onboarding and firmware versions. See [Wi-Fi matrix](LOCAL_WIFI_COMPATIBILITY.md).
- Hydronic/ground-source: use model-approved Panasonic HeishaMon, NIBE Modbus, Thermia Genesis and Luxtronik protocols; retain DHW and flow-temperature semantics distinct from air-to-air. See [protocol survey](GSHP_ASHP_ILP_LOCAL_PROTOCOLS.md).
- IR fallback: model-specific supported encodings and independent state/room measurements, no invented readback. Use only where native local control is unavailable. Review licensing before reuse.

The aim is broad protocol compatibility, not an assertion that every device is already supported. Report separate levels: upstream report, host codec, firmware read/control, model field-verified.
