# Third-party software notices — Enevina Lite

Enevina Lite's own source code is made available under [the Enevina Lite Noncommercial Community License](LICENSE), **only insofar as Enevina Oy owns or is authorized to license the relevant rights**. Third-party code is **not** relicensed under Enevina's noncommercial terms. Original rights and notices are preserved.

This inventory is based on repository build configurations and source imports as checked on 18 November 2026. The actual set of linked components must be re-generated from the **exact firmware build** before distributing firmware, a device containing firmware, or a toolchain.

| Dependency | Role / evidence in repository | Upstream license reference | Release action |
| --- | --- | --- | --- |
| Arduino-ESP32 **2.0.17** (PlatformIO framework package `3.20017.241212+sha.dcc1105b`) | `firmware/cottage/platformio.ini`, `firmware/bench/platformio.ini`, `Arduino.h`, `WiFi.h`, BLE API | [LGPL-2.1 license in Arduino-ESP32 v2.0.17](https://github.com/espressif/arduino-esp32/blob/2.0.17/LICENSE.md) | Include the complete applicable LGPL text and notices; check modified-library source availability, relinking/replacement obligations, static linking, and all distribution circumstances with legal review. |
| **NimBLE-Arduino** **1.4.3** (h2zero/NimBLE-Arduino@1.4.3) | ESP32-S3 RuuviTag BLE scanning in firmware/cottage/src/cottage_air_service.cpp, pinned in firmware/cottage/platformio.ini; replaces the bundled Bluedroid host | [NimBLE-Arduino Apache-2.0 LICENSE](https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/LICENSE) | Preserve the full Apache-2.0 text, copyright and transitive NimBLE notices with any distributed firmware. Verify exact package provenance in the final SBOM. |
| Espressif **ESP-IDF** (two firmware build paths, exact bundled release to verify) | `firmware/main/CMakeLists.txt`, `esp_*`, FreeRTOS and networking headers, Arduino framework dependencies | [ESP-IDF Apache-2.0 LICENSE](https://github.com/espressif/esp-idf/blob/master/LICENSE) and upstream component licenses | Record the **actual IDF versions** separately for the native ESP-IDF and PlatformIO builds; include required Apache notices and additional licenses of bundled components. |
| **cJSON** (bundled through framework/IDF; exact linked version to verify) | `cJSON.h` used in firmware | [cJSON MIT license](https://github.com/DaveGamble/cJSON/blob/master/LICENSE) | Preserve its copyright and full MIT permission/warranty notice in the distributed notice set. |
| **Mbed TLS** (via ESP-IDF; version/build selection to verify) | `mbedtls/pk.h`, `mbedtls/sha256.h` and HTTPS/TLS | [Mbed TLS upstream licenses](https://github.com/Mbed-TLS/mbedtls) | Verify applicable **version and configuration**; include the corresponding full license and attribution, accounting for historic dual-licensing options and bundled components. |
| **midea-local** 15.2.1 (unmodified upstream Python source/tests retained as reference) | `third_party/midea-local/`, commit `425bba4a05b74b619d31bf2d8b5a6baf030aa042`; native C++ AC capabilities, A0/C0/BB/B0/B1 commands and discovery in `pump/midea_ac_*` and `pump/midea_discovery_info.*` follow this reference | [midea-local MIT license](https://github.com/midea-lan/midea-local/blob/425bba4a05b74b619d31bf2d8b5a6baf030aa042/LICENSE), full text at `third_party/midea-local/LICENSE` | Preserve the MIT copyright and terms with derived native code. Python is not linked into firmware. Lua/plugin remain external submodules; their separate licenses need review before redistributing checked-out contents. |
| **msmart-ng** 2026.8.1 transport vectors and 2026.11.0 AC reference | Native C++ framing and supplementary AC semantics; 2026.11.0 reference commit `dd0994755bb078b613837038f2d67d97fab133ab`. Python is not linked into firmware | [Pinned MIT license](https://github.com/mill1000/midea-msmart/blob/dd0994755bb078b613837038f2d67d97fab133ab/LICENSE.md), full text at `third_party/msmart-ng/LICENSE.md` | Preserve the full upstream copyright and MIT terms when distributing derived protocol implementation. |
| **OpenSSL** (optional host tests only) | Native Midea protocol library and transport test harness on developer computers | [OpenSSL Apache-2.0 license](https://www.openssl.org/source/license.html) | Only include appropriate notices if OpenSSL is distributed with development tools; ESP32 firmware uses Mbed TLS instead. |
| **FreeRTOS kernel** (framework-managed) | `freertos/FreeRTOS.h`, RTOS tasks and synchronization | [FreeRTOS-Kernel upstream](https://github.com/FreeRTOS/FreeRTOS-Kernel) | Verify the actual ESP-IDF fork's notices, licensing and any modifications; reproduce required notices. |
| **NumPy** (development/test only) | `tools/reference_benchmark.py` | [NumPy license](https://github.com/numpy/numpy/blob/main/LICENSE.txt) (BSD-style) | Only if redistributing the Python tools with their dependencies: include or reference complete applicable notices in that distribution. |
| **SciPy** (development/test only) | `tools/reference_benchmark.py`, including `scipy.optimize.milp` | [SciPy license](https://github.com/scipy/scipy/blob/main/LICENSE.txt) (BSD-style) | Check any bundled binaries and transitive notices before redistributing a Python environment. |

## Gree protocol references

The Gree codec is independently authored C++ using documented packet formats and wire constants. No GPL source, tests, or Python runtime dependencies are included.

- [tomikaa87/gree-remote protocol description](https://github.com/tomikaa87/gree-remote/tree/828c9ec77f25d4475cfca63dd5eb81128dbc24a0) at `828c9ec77f25d4475cfca63dd5eb81128dbc24a0`, GPL-3.0, describes UDP discovery, binding and V1 packet fields.
- [cmroche/greeclimate](https://github.com/cmroche/greeclimate/tree/ec124ec8b86a9c9dcf61a783609e57964408c31c) at `ec124ec8b86a9c9dcf61a783609e57964408c31c`, GPL-3.0, documents V2 AES-GCM interoperability and response variants. Its Python dependencies are not part of Lite.

## Daikin legacy protocol references

The local HTTP adapter is independently authored C++. The references supplied wire field names and observed firmware distinctions.

- [Apollon77/daikin-controller](https://github.com/Apollon77/daikin-controller/tree/6d6d23eae45d57d04dff2ab811a30f81bce4093f), commit `6d6d23eae45d57d04dff2ab811a30f81bce4093f`, MIT. No Node runtime or source is bundled.
- [fredrike/pydaikin](https://github.com/fredrike/pydaikin/tree/3c88d3fe1ccd64e94c7d54ce52643f2c44cf6058), commit `3c88d3fe1ccd64e94c7d54ce52643f2c44cf6058`, GPL-3.0. No source, fixtures or Python runtime from this project are bundled.

WF-RAC framing follows the MIT-licensed [Mitsubishi-WF-RAC-Integration reference](https://github.com/blues-sechseck/Mitsubishi-WF-RAC-Integration/tree/4acf985ccc8ce2859c7322ccbfa6ff4bbe818174). The native C++ temperature conversion tables were adapted from [pywfrac at 39a5568](https://github.com/blues-sechseck/pywfrac/tree/39a5568901ed6897ba1045ab2b0453d7bc8c53e0). Preserve [the MIT notice](third_party/pywfrac/LICENSE) when distributing those tables. Lite uses its own C++ framing and has no Python, Home Assistant or cloud dependency for this adapter.

## Mandatory release checks

1. Build each supported firmware variant reproducibly from the pinned toolchain and record exact framework, component, library and bootloader versions and SHA-256 of deliverables.
2. Inventory **all direct and transitive linked components**, including notices shipped by the framework, toolchain runtime and any bundled binary libraries. This table is a starting point, not a complete SBOM.
3. Provide the *actual license texts and applicable copyright/NOTICE files with the distributed artifact*, not just the summary above. Where license terms require corresponding source or practical relinking, provide a compliant delivery method before a binary/device release.
4. Verify third-party assets, icons, fonts and firmware blobs separately if introduced later. No external dashboard fonts or CDNs are intentionally bundled in the currently checked dashboard.
5. Do not claim independent legal approval of the custom Enevina license or of an LGPL-compliant firmware distribution until separately reviewed.
6. When unsure about requirements, do not distribute a prebuilt firmware/device package until the legal and technical checks are complete.

**Status:** Source-code developer preview only (`v0.1.0-alpha.1`); no separately packaged firmware binary released with that GitHub Release. This does **not** waive third-party notices for future distributions.
