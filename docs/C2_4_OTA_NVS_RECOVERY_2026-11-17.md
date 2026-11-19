# C2.4 OTA NVS/rollback – source-level recovery evidence (2026-11-17)

## Scope

GitHub PR #7, recovery implementation based on original signed-pull OTA.
No firmware was uploaded, no NVS/bootloader/eFuse was written on a physical
device in this change. The physical flash backup remains incomplete.
**Physical reboot/power-cut/rollback tests of this new revision: NOT TESTED.**

## Root cause and fix

The bootloader's `PENDING_VERIFY → VALID` transition and the NVS `good`
write cannot be atomic. The old implementation reported a confirmed boot
even when NVS writing failed, and it could quarantine an image merely
because a `pending` version existed when the old app booted.

A checksummed single-blob `txv2` record tracks the release version, source
and target flash addresses, target subtype, flashed app SHA-256 and
Prepared/Armed phase. Prepared is written before boot selection, Armed only
after `esp_ota_set_boot_partition` succeeds. A candidate is accepted only
when the running target's SHA matches the record and its bootloader status
is PENDING_VERIFY or VALID, followed by the existing post-boot health gate.

After bootloader VALID, durable NVS good+cleanup must succeed before
`ota_boot_confirmed=true`. Transient NVS failures are retried, including
after a subsequent VALID reboot. Pending transactions block further OTA
until reconciliation is complete. The old running image after a
nonactivated Prepared intent is not quarantined. Confirmed rollback to
the old app quarantines an exactly matched attempted candidate, while
uncertain identity is fail-closed, not falsely marked bad.

Legacy `pending` is migrated only if the bootloader independently proves
a PENDING_VERIFY candidate; otherwise it is cleaned conservatively
without claiming a failed boot.

## ESP-IDF OTA-state compatibility P1

The independent Codex review of `0ba46e9` found that the
`esp_ota_img_states_t` numeric values are **NEW=0, PENDING_VERIFY=1,
VALID=2, INVALID=3, ABORTED=4, UNDEFINED=0xffffffff** in the installed
Arduino 2.0.17 / ESP-IDF SDK. A raw cast to the portable `ImageState`
previously treated a real pending image as NEW and could cause a false
rollback/lockout.

The new `decode_esp_idf_image_state(uint32_t)` explicitly maps these
values, and the embedded source asserts their values from its actual
ESP-IDF headers at compile-time. The portable test verifies all six
native numeric values and checks that native PENDING and VALID reach
the expected recovery decision. Rebuilt Debug 17/17, Release 17/17
and ESP32-S3 firmware (BUILD+LINK) successfully after the fix.

## Regression evidence (Core machine, isolated workspace)

Tested code branch: `feat/cottage-c24-memory-ota-safety-20261116`.
Release identity for the change series up to `8bc6f8c4ceb5` (updated with the ESP-IDF P1 fix).
Workspace: `<private-local-workspace>`.
Test logs: `/tmp/pr7_strict_dbgtest.log`,
`/tmp/pr7_strict_reltest.log`, `/tmp/pr7_strict_pio.log`,
`/tmp/pr7_nvs_test_ota_signing.log`,
`/tmp/pr7_nvs_test_c24_ble_lease_scope.log`,
`/tmp/pr7_nvs_test_c24_ota_https_nonblocking.log`,
`/tmp/pr7_nvs_upload_block.log`.

- CMake Debug / CTest: **17/17 PASS**.
- CMake Release / CTest: **17/17 PASS**.
- OTA simulated power-cut and NVS-fault scenarios are included in the
  `cottage_ota_recovery_tests` CTest target.
- Signed firmware/channel regression: **18/18 PASS**.
- BLE mutex release regression: **1/1 PASS**.
- Idle HTTPS handshake regression: **1/1 PASS**.
- ESP32-S3 PlatformIO build+link: **PASS**, 84,948 B static RAM,
  1,629,617 B firmware.
- Real PlatformIO upload target, dummy port `/dev/null`:
  **BLOCKED** with `ENEVINA_LITE_USB_UPLOAD_BLOCKED`, exit code 5.
  This was a prevention test, not physical flash.

Commands (no uploads except the intentional blocked dummy-port check):

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j4
ctest --test-dir build-debug --output-on-failure -j4
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j4
ctest --test-dir build-release --output-on-failure -j4
python3 -m unittest discover -s tests -p test_ota_signing.py -v
python3 -m unittest discover -s tests -p test_c24_ble_lease_scope.py -v
python3 -m unittest discover -s tests -p test_c24_ota_https_nonblocking.py -v
cd firmware/cottage
pio run -e esp32s3_cottage_shadow
pio run -e esp32s3_cottage_shadow -t upload --upload-port /dev/null
```

Independent review and Codex's review of the final HEAD are required
before PR merge. The pinned source configuration and full 24/48-hour
device/OTA validation remain separate physical acceptance gates.
