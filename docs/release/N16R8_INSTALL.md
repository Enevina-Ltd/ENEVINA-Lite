# ESP32-S3 N16R8 installation and recovery

N16R8 denotes 16 MiB of flash and 8 MiB of OPI PSRAM. Check the module marking and the carrier board's USB and boot-mode instructions. The firmware target selects the ESP32-S3, 16 MiB flash and OPI PSRAM in PlatformIO.

## Build a complete package

Build the application and the rollback bootloader from the same checked-out source commit. Use Python with PlatformIO Core 6.1.19 for the application. The firmware configuration pins the platform, Arduino framework, toolchain and BLE dependency. The release target gives source and SDK files stable paths in the ELF debug information, which keeps the application descriptor's ELF digest reproducible.

```sh
python -m pip install platformio==6.1.19
python -m platformio run -d firmware/cottage -e esp32s3_cottage_release
```

Activate the Espressif ESP-IDF 5.5.5 environment for the bootloader. The verified amd64 container is `espressif/idf@sha256:6e2800a69f1c6521a5651da524f811e237d13e34cad369687916d0ad0bc4ef89`. From the repository root run:

```sh
idf.py -C firmware/cottage/bootloader-rollback-idf55 -B build build
python tools/cottage_install_package.py \
  --idf-build firmware/cottage/bootloader-rollback-idf55/build \
  --pio-build firmware/cottage/.pio/build/esp32s3_cottage_release \
  --source "$(git rev-parse HEAD)" --output enevina-lite-n16r8.zip
```

The installer takes offsets from the ESP-IDF flash metadata and the compiled partition table. It verifies the rollback flag in the generated SDK configuration, both image checksums and SHA-256 digests, the matching A/B layout and empty initial OTA sectors. The test application from the ESP-IDF project stays out of the package. The ZIP contains the Lite application, rollback bootloader, partition table, initial OTA data, address map, per-file checksums and a combined first-install image.

## First installation

1. Verify the archive checksum and the files listed in `SHA256SUMS`. Use the dedicated test board for commissioning a development build.
2. Put a blank ESP32-S3 in USB download mode. Identify the intended serial port and flash capacity with esptool 4.11.0 before writing.
3. Run the addressed-component command in `INSTALL.txt`, replacing `PORT`. The combined `first-install.bin` is also provided for a blank board. It includes erased gaps and overwrites existing NVS.
4. Reboot into Lite and join its Wi-Fi setup network. Select Finnish or English, connect to the home network, then finish the pump and control setup from the local dashboard.

## Signed updates and recovery

A build with a signed channel uses the dashboard's update command. The existing firmware verifies the update signature and application digest before selecting the inactive bank. Boot health checks confirm the new bank. A failed pending image can roll back to the previous confirmed image.

Use independent publisher keys and HTTPS authorities for test and field channels. Prepare the public CA certificate and publisher verification key with `tools/cottage_ota_generate_header.py`, then build each channel separately. Publish with `tools/cottage_ota_publish.py` and keep TLS and publisher private keys in the signing environment. Deploy the manifest and app as `manifest.txt` and `firmware.bin` in the same directory on the configured HTTPS authority. A static project site can serve both files under a project prefix such as `/ENEVINA-Lite/ota/field/`. The packaged address map records the compiled channel marker when one is available. An installer built without the OTA trust configuration can be used for local installation and has no online update channel.

For USB recovery, back up the device settings with the owner's approved procedure first. Use the addressed components to leave NVS and the history partition untouched. Writing the initial OTA data selects the newly written first application bank and resets the active-bank selection. Use the board's signed update path for normal application upgrades.

The publishable binary needs the copyright notices and corresponding source described in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). See the [ESP-IDF reproducibility guidance](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/reproducible-builds.html) for the bootloader build option.

The [22 November N16R8 bench record](../field/N16R8_ROLLBACK_USB_TEST_20261122.md) documents USB staging, physical rollback and settings retention on the existing test board.
