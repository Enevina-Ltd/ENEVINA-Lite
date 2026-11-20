# Heat-pump compatibility and development status

**Status as reviewed on 19 November 2026.** This is a developer preview, **not** a certified compatibility list. A supported protocol alone does not establish electrical compatibility, safe physical commands, accurate telemetry or a manufacturer's endorsement.

## Compatibility matrix

| Pump / interface | Code or research currently present | Evidence / limitations | Consumer-ready? |
| --- | --- | --- | --- |
| Mitsubishi Electric **MSZ-FD25VA / MUZ-FD25VA** (CN105 via a local bridge) | First experimental integration, telemetry and command-path development | Source/host tests and selected pilot observations; actual CN105 UART-origin freshness and model-specific safety are not fully independently proved | **No** |
| Other Mitsubishi Electric models with a **compatible CN105 interface** | Potential adapter family, not a universal model catalogue | Model, board revision, wiring, telemetry and control semantics must each be verified | **No** |
| Midea / compatible OEM **V2 or V3 local Wi-Fi module** | Native C++ V2 direct 5A5A AES-128-ECB/MD5 or V3 authenticated 8370 session; experimental manual AC 0x40 control with independent readback; [technical notes](MIDEA_V3_LOCAL_WIFI.md) | Host protocol vectors and ESP32-S3 build still require independent verification on the new V2 changes. **V2 uses a shared static key, not device authentication.** Lite V2 has never been field-tested; V3 Frezzer Nordic already works separately in Core. Manual Midea command path exists but no physically verified Lite-to-Midea write or readback. | **No** |
| **Frezzer Nordic (owner's basement unit; exact retail submodel unrecorded)** | **Already verified through Enevina Core as a Midea-family V3 LAN device**, using `msmart-ng` and authenticated local state polling. Core code also implements HEAT, ON/OFF and temperature setpoint control paths; the earlier pilot was left `control_enabled=false`, `dry_run=true` | This is the previously documented basement **Midea** pilot, not a second untested pump. **Core V3 confirmed; ESP32 Lite not physically tested, and Lite Midea manual control not physically verified.** The exact retail model and installed module must not be inferred from generic K12/OSK102 manuals | **No** |
| Panasonic / **HeishaMon** | Read-only / hydronic reference adapter and research | Not an approved complete Lite controller configuration | **No** |
| NIBE / **Modbus** | Architecture and possible future model-specific adapter | No generally validated Lite device configuration | **No** |
| Other heat pumps with local Wi-Fi, IR or wired interfaces | Research and adapter candidates | No blanket claim of compatibility, local access or cloud-free control | **No** |

**One Lite controller is designed for one heat pump.** Other household loads, batteries, EV charging and whole-home EMS are outside Lite's scope.

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