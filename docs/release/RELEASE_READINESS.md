# Enevina Lite — Release readiness and history hygiene

Checked: **18 November 2026**. Scope: the public source repository and developer preview `v0.1.0-alpha.1`.

## Confirmed

- The first release is a **pre-release**, with a tag pointing to the `main` commit at publication; no separately uploaded firmware binary accompanies it.
- The current source tree has a custom Enevina noncommercial license, a contributor guide and general-purpose product-facing documentation. This is **source-available**, not OSI open source.
- A local read-only Git-object scan examined **430 commits / 718 blobs** reachable from the cloned branches and tags. No high-confidence committed private signing keys, GitHub access tokens or AWS access keys were identified by the patterns used. Pattern-based scanning **cannot certify absence of secrets**.
- Two apparent password-assignment hits came from human-readable Wi-Fi error-message constants, not credentials. Test MAC values, reserved private IP examples and test coordinates may appear in source/test fixtures.
- The current main branch's source and documents were scanned for named site references. Older commits **do contain identifying test-site text**, even after it was removed from the current branch.

## Blocking work before claiming privacy-clean Git history

The original source repository was publicly accessible **before** the history audit. Some older file versions, merged branches, PR refs and the published tag can retain identifying test-site prose. Merely editing the current file or adding `.gitignore` **does not remove those objects**.

A proper history purge, if desired, requires all of the following:

1. Take an **offline, access-controlled mirror backup** of the complete repository (branches, tags, release metadata and relevant pull request references).
2. Inventory every remote branch, tag, release, open/closed PR ref and any other Git ref containing the content. Run a maintained history filter on a **disposable clone**, preserving unrelated code and verifying the filtered result on every rewritten ref.
3. Review history rewrite impact: all rewritten SHA identifiers, collaborator clones, open PR diffs, CI provenance, external links and especially `v0.1.0-alpha.1` and its automatically generated source archives.
4. Publish the filtered refs with protected, reviewed **force-with-lease** operations only when the repository is quiescent. Update/recreate affected releases and tags as necessary and communicate required re-clones.
5. Contact GitHub Support for guidance on purging cached views or unreachable sensitive blobs and PR refs. Already downloaded forks and local clones cannot be recalled.
6. Rotate any actual credentials if ever discovered; rewriting Git history does not make exposed secrets safe.
7. Re-clone **all** visible refs and repeat the history and secret scan. Confirm the release tag and files are the intended approved snapshot.

**Not completed by this documentation change:** the destructive multi-ref history rewrite, retagging or cache purge. The public historical content must not be described as removed until independently verified. This work must not happen implicitly as part of a normal documentation PR.

## License and binary release checklist

- [x] Document third-party dependencies and external license references in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
- [x] Correct README's outdated statement that the repository is not yet publicly released.
- [ ] Obtain an independent review of the custom Enevina license and any contributor grants needed for future commercial licensing.
- [ ] Produce exact firmware-component SBOM from actual production builds; include full applicable third-party license texts, notices and any source/relinking obligations.
- [ ] Complete supported-device tests, OTA/recovery verification, hardware safety checks and product compliance before publishing installable firmware.
- [ ] Complete the history purge process above and confirm there are no site-identifying references in accessible historic refs, **if public history must be privacy-clean**.

## N16R8 build evidence

The [installer guide](N16R8_INSTALL.md) describes the complete 16 MiB flash and 8 MiB OPI PSRAM package. The bundled `linked-archives.json` lists archive and object members from the active sections of the exact release build's application and bootloader linker maps. It excludes discarded input sections and preserves separate application and bootloader provenance.

| Component | Source or package used |
| --- | --- |
| Arduino-ESP32 application framework | PlatformIO `3.20017.241212+sha.dcc1105b`, [Arduino-ESP32 dcc1105b](https://github.com/espressif/arduino-esp32/tree/dcc1105b0cf1322a437b354c336f2abf72b7e512) |
| Application SDK | ESP-IDF 4.4.7 family, [v4.4.7 source](https://github.com/espressif/esp-idf/tree/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3). The packaged Arduino SDK contains prebuilt archives. Record their individual source and notices during distribution review. |
| Rollback bootloader | [ESP-IDF 5.5.5 source](https://github.com/espressif/esp-idf/tree/b774170ff46c393eeb5e495ea37936038d3f4f4f) and amd64 container digest `sha256:6e2800a69f1c6521a5651da524f811e237d13e34cad369687916d0ad0bc4ef89` |
| BLE | [NimBLE-Arduino 1.4.3](https://github.com/h2zero/NimBLE-Arduino/tree/e2b40749ef1e1aae9b1f5b2647d2bbadc5abb44f) |
| Application compiler/runtime | PlatformIO xtensa-esp32s3 package `8.4.0+2021r2-patch5` |

The archive includes the tracked notices for Lite and the adapted driver sources. Before a public binary release, review the actual SDK and toolchain-runtime archives against their individual notices and source terms. In particular, review the vendor Wi-Fi, Bluetooth and PHY libraries, the Arduino SDK's bundled components, and delivery of the Arduino framework source and relinking material.

A local rebuilding exercise used main commit `9840facd7c2e698651b6574d347767f99e58305a`. It copied the pinned framework package, added a fixture to the Arduino Preferences library, and replaced the registry dependency in the test checkout with `framework-arduinoespressif32 @ symlink:///absolute/path/to/copied-package`. Building `esp32s3_cottage_release` included the fixture in the resulting application. The test ELF and binary were kept local. This verifies the rebuild mechanism. The distribution needs its own complete corresponding source and notices.

Physical USB boot and rollback are documented in the [N16R8 bench record](../field/N16R8_ROLLBACK_USB_TEST_20261122.md). A live signed update over Wi-Fi and a blank-board installation still require verified bench results. The public alpha.2 asset predates the complete installer.

## Preventive checks

`.gitignore` excludes local credentials and private signing-key patterns. This reduces accidental additions but **does not protect already-tracked files, Git history, or uploaded artifacts**. Run additional dedicated secret-scanning software across the full history whenever dependencies and tools permit. Do not commit output containing actual secrets.
