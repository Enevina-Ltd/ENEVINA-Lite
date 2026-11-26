# Local Midea enrollment (V2/V3)

## User flow / käyttöohje

1. Connect the Lite and heat pump Wi-Fi module to the same trusted home LAN.
2. Choose Midea in the setup wizard and press **Etsi Midea lähiverkosta /
   Find Midea on the local network**. Select the heat pump if there is more
   than one. The LAN reply supplies the address, protocol and appliance ID;
   known non-AC types and ambiguous identities cannot be selected.
3. V2: press **Yhdistä lämpöpumppu / Connect heat pump**. No device keys,
   manual address or cloud account are required.
4. V3: import an existing Midea Connect device-specific credential backup
   (JSON, format midea-smart-ac-credentials, version 1, device_type AC,
   port 6444) for the selected appliance, then Connect. The backup address
   is ignored. The identity must match and credentials are validated without
   exposing them in the normal wizard. If you already use Midea Connect,
   its device options provide the saved credential export; save that JSON
   privately as a file. Delete unnecessary copies afterwards. Advanced
   settings retain the manual fallback.
5. The existing Midea worker authenticates V3 (V2 uses the legacy local
   protocol) and requires a real basic-state response matching the appliance
   ID before committing the credentials. An enrollment job has a bounded
   lifetime; polling its ID cannot mistake an older successful state for a
   new pairing. Pairing is not permission to write to the pump. Continue the
   normal wizard after a fresh read; physical control uses the existing
   model/temperature-limit approval, and the optimizer uses the same adapter.

## First-time V3 limitation

A new V3 pump without existing credentials **cannot be enrolled by Lite
alone using this implementation**. No supported public local key derivation
or manufacturer-approved cloud enrollment interface suitable for commercial
embedding was verified. Do not market this as fully automatic V3 onboarding.
The backup workflow is a migration path, not a substitute for first-time
manufacturer authorization. For a completely new device, investigate an
official manufacturer authorization/partner interface; do not ship embedded
third-party app credentials or depend on a legacy cloud endpoint. Midea
Connect can provision Wi-Fi over BLE on supported models, but its portable
backup still requires the device credentials in the integration first.
An appropriate direct-UART module is an alternative hardware path, not the
same Wi-Fi V3 protocol.

Research references, reviewed 2026-11-26:
- [Home Assistant Midea integration](https://www.home-assistant.io/integrations/midea/)
- [Midea Connect source / portable credential format](https://github.com/fiam/home-assistant-midea-connect/blob/e3ad37662a51ec2b0d95a95c1164c14b8eab7d21/custom_components/midea_connect/credential_backup.py)
- [midea-local](https://github.com/midea-lan/midea-local/tree/192718afb43f1a69741e088cedd39820240c28a3)
- [msmart-ng](https://github.com/mill1000/midea-msmart/tree/e92efc5166d3bdaad43993cf63828a272dd65616)
- [ESPHome Midea UART](https://esphome.io/components/climate/midea/)

Community cloud-API implementations do not establish manufacturer approval,
current service availability for every account, or commercial licensing.

## Recovery and security

A connection failure can schedule a rate-limited scan through the existing
loop discovery service. Only a unique hit with the saved device ID and
protocol, compatible type and current network/configuration generation is
eligible. The worker verifies the candidate before persisting an address
change. Pending or active commands prevent reconfiguration. The same-device
recovery retains existing approvals and unresolved command reconciliation,
and requires a fresh normal read before control resumes. No new task or
watchdog is used. Calibration identity is now protocol + device ID, not IP;
an existing IP-dependent calibration identifier will change once on upgrade.

Credentials stay in the current NVS settings layout and are not returned by
the API. The development firmware uses **HTTP on a trusted home LAN**, not
encrypted credential transport; NVS encryption/secure provisioning has not
been verified on the test hardware. Neither should be represented as a
production-grade secret-storage guarantee. A commercial release needs a
verified protected enrollment channel and encrypted storage or a hardware
provisioning/security decision. No new Lite login, account, PIN, continuously
connected manufacturer cloud, Enevina server or HA dependency was introduced.

The helper tools/frezzer_pair_from_core.py is for the authorized existing
Core integration on a trusted LAN only. It now discovers the matching pump,
submits the enrollment job and requires fresh verified state; it expects
the new firmware API.

## Acceptance scope

The previously installed test firmware was physically read-only paired with
an existing V3 credential set, yielding a fresh state and capabilities.
That proves LAN communication, not new firmware enrollment, first-time cloud
authorization, DHCP recovery, network isolation, or a control command.
Host tests exercise the new worker and actual UI handlers; firmware and
physical acceptance results must be recorded separately for the exact build.
