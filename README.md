# Enevina Lite

**A local-first, source-available heat pump controller by Enevina Oy.**

Enevina Lite is an ESP32-S3-based controller for **one heat pump per device**, designed for local operation without a mandatory cloud service or Home Assistant. Its goal is to reduce heating costs using electricity prices, weather forecasts and deterministic, physics-based optimization (MPC). **Lite does not contain AI/ML.** Manufacturer/model support depends on separately developed and verified adapters.

**Development status:** This is an evolving prototype, not a general claim of compatibility, certified safety or production readiness. Check the model-specific documentation and tests before connecting to a real heat pump. Enevina Core and Adaptive Intelligence are separate products.

## License and commercial use

**Enevina Lite is source-available, not OSI-approved open source.**

- **Community license:** [Enevina Lite Noncommercial Community License 1.0](LICENSE).
- **Free noncommercial use:** study, hobby use, experiments, modifications and free noncommercial sharing are permitted under the license terms.
- **No third-party sales or commercial exploitation:** selling or commercially distributing Enevina Lite software or firmware, or manufacturing and selling controllers that include it, requires a **separate written commercial license from Enevina Oy**.
- **Third-party libraries and assets** keep their own licenses, which may impose additional obligations. This community license applies only to rights Enevina Oy can legally grant.
- **Contributions:** see [CONTRIBUTING.md](CONTRIBUTING.md) before submitting code intended for possible inclusion in Enevina Oy's commercial products.
- The Enevina name and logo are not freely licensed as product brands.

These are software-license conditions. They do not override mandatory legal rights or separately licensed third-party components. The repository and first developer preview are public. This custom license has **not yet received independent legal review**; publication is not a legal endorsement. See [third-party notices](THIRD_PARTY_NOTICES.md) and [release audit](docs/release/RELEASE_READINESS.md).

## Product scope

Enevina Lite is a lightweight standalone controller designed for **one heat pump per device**.
The goal is local operation without requiring Home Assistant, a subscription, or a continuous cloud connection.
Public electricity prices and weather forecasts can be used for optimization when available, with
a locally defined fallback during outages.

- **Hardware:** ESP32-S3; Wi-Fi, with optional model-specific gateways or interfaces.
- **Control:** manufacturer-specific local adapters; support must be verified for each model.
- **Optimization:** deterministic building physics and price-aware MPC, using 15-minute periods; **no AI**.
- **Measurements:** compatible heat-pump telemetry and optional temperature, humidity, and power sensors.
- **User interface:** local browser dashboard for status, settings, measurements, and optimization plans.
- **Updates:** signed OTA and recovery mechanisms are under active development and verification.

## Architecture

| Component | Responsibility |
| --- | --- |
| `pump/` | Model-specific interfaces, capabilities, telemetry, and adapter contracts |
| `physics/` | Thermal response and heat-pump performance models |
| `forecast/` | Electricity prices, weather forecasts, and data validation |
| `optimizer/` | Deterministic heating-cost optimization |
| `safety/` | Safety constraints, source freshness, command validation, and fallback |
| `firmware/` | Embedded networking, local UI, sensors, and OTA integration |
| `tests/` | Host-side tests, simulations, and regression checks |

## Development and verification

This repository contains prototypes, reference algorithms, test tools, and firmware
under active development. Features and compatibility are not guaranteed for all heat-pump
models. Some historical reports refer to earlier test configurations and are not a
statement of current production readiness.

Build the native C++ project and run its tests:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The ESP32-S3 firmware uses the toolchain specified in its relevant
`platformio.ini` or ESP-IDF project files. Review hardware compatibility and
the model-specific safety requirements before attempting installation.

## Safety and contributions

Never assume that a successful simulation, build, or mocked device response
proves that physical heat-pump control is safe. Do not interrupt a heat pump's
mains supply for price optimization. Respect manufacturer limits and verify
the actual device interface before issuing commands.

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md) before
submitting changes. Commercial licensing requires separate written permission
from Enevina Oy as defined in [LICENSE](LICENSE).
