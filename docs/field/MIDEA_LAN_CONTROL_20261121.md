# Midea V3 manual control: Frezzer Nordic field result

**Date:** 21 November 2026. **Result:** PASS for the bounded setpoint round trip
described below. No private addresses, device IDs, serial numbers, pairing
secrets or raw physical frames are published.

## Device and software

| Item | Verified value |
| --- | --- |
| Pump | Frezzer Nordic, Midea residential AC family; exact retail submodel not recorded |
| LAN protocol | Authenticated Midea V3, appliance type `0xAC` |
| Response family | Classic C0 with a short body; some optional fields absent |
| Controller | ESP32-S3 N16R8 test board |
| Firmware | `2026112102`, signed OTA, A/B image boot-confirmed VALID |
| Reviewed source | `afe93820cf031bcb6c3952b64595653e828e05fe` |
| Merged main | `e1e177a432373af51f548f9a2d8b542c04ccb3d5` |
| Source tree | `d104f60abcc5dbdd284f034a6c1bf5304988ad44` |
| Firmware SHA-256 | `16856b6aa64b19fdbd06a259dd02031e861730a02e3f36d8d7b91f4137039680` |

Source and build provenance are also recorded in [PR #70](https://github.com/Enevina-Ltd/ENEVINA-Lite/pull/70).

## Physical experiment

The owner authorized updating this test board and a bounded manual setpoint
experiment. A temporary device-specific pilot allowed 22–23 C. Automatic
optimizer control remained disabled throughout.

| Step | Lite result | Separate Core `msmart` GetState read |
| --- | --- | --- |
| Baseline | OFF, HEAT, 22 C | OFF, HEAT, 22 C |
| One request for 23 C | `control_code=ok`, new confirmation timestamp, 23 C | OFF, HEAT, 23 C |
| One request restoring 22 C | `control_code=ok`, new confirmation timestamp, 22 C | OFF, HEAT, 22 C |

The independent reader authenticated separately from Lite. HTTP acceptance
alone was not treated as success. Each Lite command also performed its own
fresh pre-read, one SET and separate queried confirmation.

Reported fan, swing, eco, turbo, sleep and anion values remained unchanged.
Missing frost telemetry stayed unknown; the compatibility command default was
not promoted to a measurement. The test script exited **0**, with no failures.

## Restored state and limits of the evidence

- Final state: **OFF / HEAT / 22 C**, with fresh successful state readback.
- Temporary pilot permission revoked; no pending or busy command.
- Automatic control remained disabled and its command counter stayed zero.
- Persisted settings before/after OTA were identical.
- Original OTA distribution configuration restored and service active.

This verifies the tested model's short-C0 setpoint path. It does not exercise
compressor operation, a power-on change, every preset, V2 hardware, BB hardware,
WAN blocking or cloud-free initial pairing. Other compatible models use the
same native adapter but retain source/protocol evidence until physically tested.

[Original validation receipt](https://github.com/Enevina-Ltd/ENEVINA-Lite/pull/70#issuecomment-7063749539)
and [model catalogue](../integrations/MIDEA_MODEL_CATALOG.md).
