# N16R8 test board rollback, 22 November 2026

| Item | Verified result |
| --- | --- |
| Test unit | The exact USB JTAG identity matched a responding Lite on the home network. |
| Memory and startup | ESP32-S3 revision 0.2, 16 MiB flash, 8 MiB embedded PSRAM and an ESP-IDF 5.5.5 rollback bootloader. |
| Original firmware | Signed build 2026112103, app1 marked valid, automatic control disarmed. |
| Test application | Build from main commit `b6baf01c25b1cee2bf09b1e027645212287af354`. N16R8 archive SHA-256 `50aa1947e15d0019f38c11558310258c08fb58e1a5a7980bebb929bc35f429df`. |
| Flash regions checked | Bootloader starts at `0x0`. The board's partition table at `0x8000` matches the archive. OTA data is at `0xe000`. App0 at `0x10000) and app1 at `0x650000) are each `0x640000` bytes. |

The test used the existing two-bank installation. We backed up the original standby application and OTA metadata, verified the installed partition table byte for byte, and staged the new application in app0 via USB. The live app1 image and NVS were outside the write ranges. The new application started with pending verification, joined the saved Wi-Fi network, preserved the checked site settings and read a fresh OFF state from the paired Midea V3 pump. Automatic control remained disarmed throughout the test.

This USB-staged image had no signed transaction. The running application did not confirm it and the bootloader returned to the original valid app1. We restored the earlier standby image and OTA metadata and verified an exact flash readback. The first check after the verification reset timed out. The final normal USB reboot restored the local dashboard in 10 seconds and a fresh Midea OFF reading in 38 seconds. No heat command was sent.

A separate offline test signed a build for one temporary channel and verified rejection under a second channel's trust configuration. The physical test covered USB staging, pending boot, automatic rollback, settings retention and restoration. Signed pull over Wi-Fi and first installation on a blank board still need their own bench tests. This record contains no device addresses or channel private material.
