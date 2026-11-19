# Frezzer Nordic / Midea V3 – ESP32-S3 integration test

**Status:** Source verified by host tests and ESP32-S3 PlatformIO build.
The owner's basement Frezzer Nordic is already a V3 Midea LAN pilot in Enevina
Core. This **does not mean that the ESP32 Lite client has passed field testing**.

## Test target and protections

- Test **one Frezzer Nordic** with a dedicated **ESP32-S3 N16R8** (16 MB flash,
  8 MB OPI PSRAM) whose hardware, bootloader, partitions and rollback are
  confirmed before installing the image. Do not flash the production Core
  host or replace the working Core Midea integration.
- This is a **read-only** Midea pilot. No HEAT, ON/OFF, setpoint, CN105 or
  optimizer write is reachable when Midea is selected.
- Keep Core physical Midea control unchanged during testing.
- No Wi-Fi/LAN device IDs, tokens, keys, MACs or addresses belong in Git.
- Initial Lite Midea pairing credentials travel over **unencrypted local HTTP**
  and are saved as plaintext in Lite NVS. Use this only on a trusted test LAN.
  It is not a commercial-ready credential security design.

## Prepared firmware

Build from the branch associated with PR #48, including the Frezzer test
diagnostic changes. The build environment is
`firmware/cottage/esp32s3_cottage_shadow` in `platformio.ini`.

The Core development machine has the test source and generated `firmware.bin`
at `/home/kaelion/dev/enevina-lite-frezzer-test-20261119/firmware/cottage/.pio/build/esp32s3_cottage_shadow/firmware.bin`.
A checked image is not by itself an approved flash/OTA installation: verify
the exact board flash layout, the existing rollback state and USB/OTA
installation method before writing anything.

## Evening test sequence

1. On the separate Lite ESP32-S3, complete **Wi-Fi first** on the existing
   browser onboarding page. Then enter its **Settings** page.
2. Choose **Midea / OEM – V3** under *Lämpöpumppu ja yhteydet*, and save.
   This disables Mitsubishi's CN105 physical control path.
3. From **Core**, reuse the **already working Frezzer V3 credentials** without
   copying secrets into public code:
   ```sh
   /opt/kaelion/.venv/bin/python /home/kaelion/dev/enevina-lite-frezzer-test-20261119/tools/frezzer_pair_from_core.py --check-only
   /opt/kaelion/.venv/bin/python /home/kaelion/dev/enevina-lite-frezzer-test-20261119/tools/frezzer_pair_from_core.py --lite-ip <LITE_PRIVATE_IP>
   ```
   The first command is offline and only checks Core credentials.
   The second performs local HTTP enrollment of one selected ESP32.
   The tool does not print or persist Core secrets and does not send pump
   commands. Use only your trusted LAN; it does not solve HTTP eavesdropping.
4. In the Lite Midea card click **Testaa yhteys nyt**. This schedules a
   read-only V3 status query. Status refreshes every 30 s; reload the Midea
   card if necessary. Check **last attempt**, connection result, indoor
   temperature, requested setpoint, operating mode and outdoor temperature.
5. Optionally inspect `GET /api/midea/status`: it exposes only sanitized
   reading/diagnostic fields and **never token, key, device ID or host**.
   Compare the result with Core's existing Midea readings; don't infer a
   successful control write from a readable state.

## Troubleshooting

| Lite diagnostic | What to verify |
| --- | --- |
| `not_selected` | Set Midea as selected pump and save |
| `not_configured` | Run local Core pairing helper after Wi-Fi setup |
| `invalid_address` | Check the Frezzer host in the Core config |
| `connection_failed` | Same LAN/VLAN reachability and TCP/6444 |
| `authentication_failed` | Midea V3 credential pair; confirm Core still works |
| `time_not_ready` | ESP must have correct synchronized UTC time |
| `protocol_failed` | V3 frame/auth checksum or incomplete TCP reply |
| `unsupported_state` | Real Frezzer C0 state format needs examination |
| `ok` and `fresh=true` | Compare reported temperatures and mode with Core |

If the test returns other than `ok`, record **only** the public error code
and firmware version. Do not record raw authenticated packets or secrets in
a public issue.

## Explicitly not yet proved

Physical Lite communication with Frezzer; accuracy of all C0 fields; Lite
Midea setpoint writes, write readback and optimizer interaction; secure
credential storage or secure OTA installation on the actual test card.