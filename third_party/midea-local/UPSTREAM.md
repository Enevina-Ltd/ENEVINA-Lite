# Vendored Midea Local library — upstream reference, not ESP32 runtime

Source: https://github.com/midea-lan/midea-local
Pinned upstream git commit: `425bba4a05b74b619d31bf2d8b5a6baf030aa042`
Library version: `15.2.1`
License: MIT (see `LICENSE`; original authors retain copyright).

This directory contains the **complete Python `midealocal/` source package**,
its upstream `tests/`, README files, changelog, packaging metadata and lockfile.
All device protocol implementations are included, not just AC.
The vendored files are unchanged from the pinned upstream commit.

The upstream repository also has two **separate Git submodule repositories**:
* model-specific Lua scripts — `midea-lan/midea-lua-lib`, pinned by upstream to
  `e700dc2cb91c7220eb0638b265581a3aec08b0be`;
* plugins — `midea-lan/midea-plugin`, pinned by upstream to
  `c2ea5bba4df4b90bd772d3fea84e21f1abcbac3b`.
Those separate projects are **not copied into this directory** and need their
own provenance/licensing checks before any redistribution.

**Firmware boundary:** Enevina Lite uses C++ and ESP32-S3. This Python source
is included for parity checks, protocol derivation and tests; it is NOT
compiled, imported or executed by the ESP32 firmware. Copying Python cannot
by itself add Midea device support to the running firmware. The runtime LAN
controller remains in `pump/midea_v3_protocol.*` and
`firmware/cottage/src/cottage_midea*.cpp`.

**Local-only design:** `midealocal/cloud.py` is part of upstream full-source
completeness, **not** permission to add a cloud dependency to Lite.
No credentials, real device identifiers or raw home captures belong in this tree.

Updates: obtain a known-good upstream commit, review its license, copy
the full `midealocal/` and `tests/` trees and listed metadata, record the
new commit and upstream test results. Never silently update to upstream HEAD.
