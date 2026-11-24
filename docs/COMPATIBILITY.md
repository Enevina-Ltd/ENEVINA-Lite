# Heat-pump compatibility and development status

**Midea status updated on 21 November 2026; other rows retain the 19 November review.** This is a developer preview, **not** a certified compatibility list. A supported protocol alone does not establish electrical compatibility, safe physical commands, accurate telemetry or a manufacturer's endorsement.

## Compatibility matrix

| Pump / interface | Code or research currently present | Evidence / limitations | Consumer-ready? |
| --- | --- | --- | --- |
| Mitsubishi Electric **MSZ-FD25VA / MUZ-FD25VA** (CN105 via a local bridge) | First experimental integration, telemetry and command-path development | Source/host tests and selected pilot observations; actual CN105 UART-origin freshness and model-specific safety are not fully independently proved | **No** |
| Other Mitsubishi Electric models with a **compatible CN105 interface** | Potential adapter family, not a universal model catalogue | Model, board revision, wiring, telemetry and control semantics must each be verified | **No** |
| Midea / compatible OEM **V2 or V3 AC type `0xAC`** | Native V2/V3 LAN; model/subtype and B5 capabilities; classic C0/A0 and BB state/commands; selective B0 properties and dynamic controls; [named models and OEM catalogue](integrations/MIDEA_MODEL_CATALOG.md) | Generic compatible models use the same adapter without a retail-name allowlist. Source/protocol coverage is separate from physical evidence. Automatic price control, thermostat and predictive MPC are experimental and have host evidence. Physical automatic control and V2 remain unverified. | **No** |
| **Frezzer Nordic (exact retail submodel unrecorded)** | Native ESP32 Lite V3 state and manual control physically verified with firmware **2026112102** | OFF / HEAT **22 -> 23 -> 22 C**, separate Lite confirmation and independent Core `msmart` reads, reported settings preserved. Unknown optional fields use reference command defaults and remain unknown as telemetry. [Dated evidence](field/MIDEA_LAN_CONTROL_20261121.md) | **No** |
| Gree / EWPE Smart LAN V1/V2 | Native local UDP discovery, pairing and state reads, optional pump temperature | Experimental HEAT, OFF and Celsius setpoint control with model-specific approval. No hardware-verified models yet. [Gree LAN](integrations/GREE_LAN.md) | **No** |
| Daikin legacy local HTTP | Native IPv4 pairing, actual module identity, control-state and optional sensors | Experimental model-approved manual and automatic heating control. Modules must expose the legacy control-state endpoint. DSIoT and BRP069C module families need separate verification. [Daikin legacy](integrations/DAIKIN_LEGACY.md) | **No** |
| Daikin DSIoT | DSIoT module identity plus indoor control and optional temperature data after a live HTTP test | Read-only. Separate from legacy HTTP. [Daikin DSIoT](integrations/DAIKIN_DSIOT.md) | **No** |
| Mitsubishi Heavy Industries WF-RAC | Native pairing and status via the legacy HTTP module. This protocol is distinct from Mitsubishi Electric CN105. | Experimental manual and automatic HEAT/OFF with a pre-existing local operator ID. HTTPS module branches require a separate transport. [WF-RAC](integrations/MHI_WF_RAC.md) | **No** |
| ECHONET Lite (compatible original LAN modules) | Native ESP32 UDP discovery, node/instance pairing, property maps, state, manual HEAT/COOL/OFF/setpoint and automatic heating | Experimental; host and firmware build evidence only; compatible module/firmware and device-level commands still require field validation. Automatic cooling not implemented. [ECHONET](integrations/ECHONET_LITE.md) | **No** |
| Mitsubishi Electric direct CN105 | Native 2400 or 9600 baud UART reads through a compatible interface | Experimental model-approved manual and automatic heating control. Requires a model-appropriate CN105 level interface and an ESP32-S3 board with the documented available GPIOs. [Direct CN105](integrations/MITSUBISHI_CN105_DIRECT.md) | **No** |
| Daikin S21 | 2400 baud, 8E2 standard S21 electrical interface via a ready-made compatible adapter | Experimental model-approved HEAT, OFF and half-degree setpoint. Configure and test the actual connector. [Daikin S21](integrations/DAIKIN_S21.md) | **No** |
| Haier | hOn and SmartAir2 direct UART | Experimental firmware control | Supported packet layouts, model-approved HEAT/OFF/setpoint and fresh bus readback. No field-verified model yet. [Haier UART guide](integrations/HAIER_UART.md) |
| Panasonic | CZ-TACG1 CN-CNT and DNSK-P11 CN-WLAN (air-to-air) | Experimental firmware control | Local bus setup, model-approved HEAT/OFF/setpoint and fresh readback. Exact indoor models need field verification. [Panasonic UART guide](integrations/PANASONIC_UART.md) |
| Midea **`0xCC` MDV/commercial AC**, **`0xC3` hydronic controller**, **`0xCD` heat-pump water heater**, **`0xCF` heat pump** | Upstream Python references are present or linked; their distinct native codecs are not implemented | They do not inherit Lite's `0xAC` support. A shared brand, V3 transport or vendored reference is insufficient. [Protocol boundaries](integrations/MIDEA_MODEL_CATALOG.md#protocol-boundaries) | **No** |
| Panasonic / **HeishaMon** | Read-only / hydronic reference adapter and research | Not an approved complete Lite controller configuration | **No** |
| NIBE / **Modbus** | Architecture and possible future model-specific adapter | No generally validated Lite device configuration | **No** |
| Other heat pumps with local Wi-Fi, IR or wired interfaces | Research and adapter candidates | No blanket claim of compatibility, local access or cloud-free control | **No** |

**One Lite controller is designed for one heat pump.** Other household loads, batteries, EV charging and whole-home EMS are outside Lite's scope.

For Midea automatic control, confirm the exact pump and temperature limits in
Settings after a successful local read, then enable automatic control. The
same LAN worker serializes manual and automatic commands. Enablement ends on
reboot or connection-configuration changes. The MPC planner uses a
source-verified CN105 observation or a paired Midea LAN read, depending on
the selected adapter. Price control and thermostat remain available during
calibration or a planning outage. See the [MPC evidence](MPC_CONTROL_20261120.md).

The [Midea model catalogue](integrations/MIDEA_MODEL_CATALOG.md) records primary
sources, exact model reports, missing features and current Lite coverage. Brand
names and internal model codes are not interchangeable, and unlisted compatible
`0xAC` models are not blocked by a name whitelist.

## Feature evidence — not marketing claims

| Feature | Development state | Key restriction |
| --- | --- | --- |
| ESP32-S3 local dashboard, Finnish/English UI | Firmware implementation and source/host UI tests | Model/data availability and physical behavior require further validation |
| Wi-Fi-first browser setup | Implemented; host-tested | Mobile and physical-device provisioning still needs targeted tests |
| Nord Pool quarter-hour pricing, local caching | Source and integration tests | Live-data, tariff and daylight-saving scenarios are configuration-dependent |
| FMI forecasts / dashboard view | Integration development | Forecasts do not yet guarantee an active field-calibrated MPC |
| RuuviTag BLE + humidity | Parser and local measurement integration | Sensor pairing and physical radio co-existence must be verified |
| Shelly power monitoring | Read-only measurement integration | A meter measuring a whole building must **not** be presented as pump-only power |
| Price-based heat control | Experimental price heuristic in the pilot path | Not the same as the model-based MPC benchmark |
| Thermal MPC | In the control chain behind a walk-forward calibration gate; host-tested and simulated | Not yet calibrated or validated on a real pump; no confirmed field savings |
| Signed OTA / recovery | Implementation and focused tests | Exact board, partition layout and rollback procedure must be approved before user distribution |

See the [optimization audit](OPTIMIZATION_AUDIT_20261118.md), [Wi-Fi onboarding notes](ONBOARDING_WIFI_FIRST_20261118.md), [CN105 measurement evidence](field/CN105_SOURCE_PROOF_20261117.md) and [release readiness](release/RELEASE_READINESS.md) for detailed sources and dated limitations.

## Request a new heat-pump model

Use [Discussions](https://github.com/Enevina-Ltd/ENEVINA-Lite/discussions) to share **manufacturer, exact model, documented local protocol, links to public technical documentation and the available read-only measurements**. Do not publish MAC addresses, private network details, installation addresses or device secrets.

## Suomeksi

**Yhteensopivuustaulukko on kehitystilanneraportti, ei luettelo myyntiin hyväksytyistä pumpuista.** Mitsubishi Electricin CN105 on ensimmäinen kokeellinen sovitin. Panasonic/HeishaMon, NIBE/Modbus ja muut paikalliset protokollat vaativat erillisen mallikohtaisen toteutuksen ja todentamisen. Fysiikkapohjaista MPC:tä on tutkittu simulaatiossa, mutta kentällä saavutettuja säästöjä ei vielä väitetä osoitetuiksi. Laitteen tai pumpun tunnistetietoja ei pidä jakaa julkisesti.
