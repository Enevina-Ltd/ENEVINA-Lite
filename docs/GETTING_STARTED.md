# Getting started with Enevina Lite

This page is for **developers and reviewers** of the public developer preview, not a universal consumer installation guide.

## 1. What you need

- Git
- CMake **3.20 or newer**
- A compiler with **C++17** support
- Optional: Python 3 for additional reference and source-level tests

No ESP32 hardware, heat pump, Wi-Fi credentials, cloud service or Home Assistant is required for the native tests.

## 2. Download and run the native tests

```sh
git clone https://github.com/linkki79-pixel/ENEVINA-Lite.git
cd ENEVINA-Lite
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, a compatible CMake generator and C++ toolchain must be installed. Build output and compiler names depend on your operating system.

These are **host tests**. A successful build is not evidence that any physical pump, network connection, or OTA installation is safe.

## 3. Explore the project

| Goal | Where to look |
| --- | --- |
| Read the local control and physics models | `cottage/physics.cpp`, `cottage/mpc.cpp`, `cottage/price_bootstrap.hpp` |
| Compare optimization policies | [Optimization audit](OPTIMIZATION_AUDIT_20261118.md) and `tools/cottage_optimization_audit.py` |
| Understand device compatibility | [Compatibility](COMPATIBILITY.md) |
| See how the Wi-Fi-first setup was tested | [Onboarding notes](ONBOARDING_WIFI_FIRST_20261118.md) |
| Investigate firmware / OTA requirements | [Release readiness](release/RELEASE_READINESS.md), `firmware/cottage/platformio.ini` |
| Contribute safely | [CONTRIBUTING.md](../CONTRIBUTING.md) |

Some scripts require separate Python libraries (for example NumPy and SciPy for the independent MILP benchmark). They are **not** needed for the basic CMake/CTest flow. Do not claim a benchmark passed without executing it in your environment.

## 4. ESP32 and physical installations

There is **no generally approved flash-and-install package** in `v0.1.0-alpha.1`. The firmware project has version-pinned toolchains and a deliberately blocked generic USB-upload path to avoid damaging the existing rollback-capable boot chain. Do **not** bypass it.

For hardware development, inspect the specific device revision, flash layout, network adapter and pump protocol first. Follow `AGENTS.md` and the model-specific tests; any new physical commands or flash operations require a separately reviewed test plan and authorization.

The development Wi-Fi onboarding uses an **open setup access point** only for Wi-Fi configuration. It is not suitable for exposure to untrusted networks as an approved production security model. Do not add account passwords or PIN gates to the development workflow without an explicit product decision.

## 5. Questions and contributions

- [GitHub Discussions](https://github.com/linkki79-pixel/ENEVINA-Lite/discussions): general questions, ideas and model-specific experience
- [GitHub Issues](https://github.com/linkki79-pixel/ENEVINA-Lite/issues): reproducible defects and tracked requests
- [Contribution rules](../CONTRIBUTING.md): licensing and code-review requirements

Never attach device secrets, private IP addresses, non-public test-site details, Wi-Fi passwords, private OTA keys, or unredacted logs.

## Suomeksi

Enevina Liten **kehittäjien esijulkaisua** voi tutkia turvallisesti tietokoneella ilman ESP32-korttia yllä olevilla CMake- ja CTest-komennoilla. Testien läpäisy ei tarkoita hyväksyttyä fyysistä pumppuohjausta. Valmista, yleiskäyttöistä firmware-asennuspakettia ei vielä ole. Kysy lämpöpumppujen yhteensopivuudesta Discussionsissa ja ilmoita toistettavat viat Issues-osiossa.
