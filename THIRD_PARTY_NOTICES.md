# Third-party software notices — Enevina Lite

Enevina Lite's own source code is made available under [the Enevina Lite Noncommercial Community License](LICENSE), **only insofar as Enevina Oy owns or is authorized to license the relevant rights**. Third-party code is **not** relicensed under Enevina's noncommercial terms. Original rights and notices are preserved.

This inventory is based on repository build configurations and source imports as checked on 18 November 2026. The actual set of linked components must be re-generated from the **exact firmware build** before distributing firmware, a device containing firmware, or a toolchain.

| Dependency | Role / evidence in repository | Upstream license reference | Release action |
| --- | --- | --- | --- |
| Arduino-ESP32 **2.0.17** (PlatformIO framework package `3.20017.241212+sha.dcc1105b`) | `firmware/cottage/platformio.ini`, `firmware/bench/platformio.ini`, `Arduino.h`, `WiFi.h`, BLE API | [LGPL-2.1 license in Arduino-ESP32 v2.0.17](https://github.com/espressif/arduino-esp32/blob/2.0.17/LICENSE.md) | Include the complete applicable LGPL text and notices; check modified-library source availability, relinking/replacement obligations, static linking, and all distribution circumstances with legal review. |
| Espressif **ESP-IDF** (two firmware build paths, exact bundled release to verify) | `firmware/main/CMakeLists.txt`, `esp_*`, FreeRTOS and networking headers, Arduino framework dependencies | [ESP-IDF Apache-2.0 LICENSE](https://github.com/espressif/esp-idf/blob/master/LICENSE) and upstream component licenses | Record the **actual IDF versions** separately for the native ESP-IDF and PlatformIO builds; include required Apache notices and additional licenses of bundled components. |
| **cJSON** (bundled through framework/IDF; exact linked version to verify) | `cJSON.h` used in firmware | [cJSON MIT license](https://github.com/DaveGamble/cJSON/blob/master/LICENSE) | Preserve its copyright and full MIT permission/warranty notice in the distributed notice set. |
| **Mbed TLS** (via ESP-IDF; version/build selection to verify) | `mbedtls/pk.h`, `mbedtls/sha256.h` and HTTPS/TLS | [Mbed TLS upstream licenses](https://github.com/Mbed-TLS/mbedtls) | Verify applicable **version and configuration**; include the corresponding full license and attribution, accounting for historic dual-licensing options and bundled components. |
| **FreeRTOS kernel** (framework-managed) | `freertos/FreeRTOS.h`, RTOS tasks and synchronization | [FreeRTOS-Kernel upstream](https://github.com/FreeRTOS/FreeRTOS-Kernel) | Verify the actual ESP-IDF fork's notices, licensing and any modifications; reproduce required notices. |
| **NumPy** (development/test only) | `tools/reference_benchmark.py` | [NumPy license](https://github.com/numpy/numpy/blob/main/LICENSE.txt) (BSD-style) | Only if redistributing the Python tools with their dependencies: include or reference complete applicable notices in that distribution. |
| **SciPy** (development/test only) | `tools/reference_benchmark.py`, including `scipy.optimize.milp` | [SciPy license](https://github.com/scipy/scipy/blob/main/LICENSE.txt) (BSD-style) | Check any bundled binaries and transitive notices before redistributing a Python environment. |

## Mandatory release checks

1. Build each supported firmware variant reproducibly from the pinned toolchain and record exact framework, component, library and bootloader versions and SHA-256 of deliverables.
2. Inventory **all direct and transitive linked components**, including notices shipped by the framework, toolchain runtime and any bundled binary libraries. This table is a starting point, not a complete SBOM.
3. Provide the *actual license texts and applicable copyright/NOTICE files with the distributed artifact*, not just the summary above. Where license terms require corresponding source or practical relinking, provide a compliant delivery method before a binary/device release.
4. Verify third-party assets, icons, fonts and firmware blobs separately if introduced later. No external dashboard fonts or CDNs are intentionally bundled in the currently checked dashboard.
5. Do not claim independent legal approval of the custom Enevina license or of an LGPL-compliant firmware distribution until separately reviewed.
6. When unsure about requirements, do not distribute a prebuilt firmware/device package until the legal and technical checks are complete.

**Status:** Source-code developer preview only (`v0.1.0-alpha.1`); no separately packaged firmware binary released with that GitHub Release. This does **not** waive third-party notices for future distributions.
