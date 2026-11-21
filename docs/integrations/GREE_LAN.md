# Gree and EWPE Smart LAN

Select **Gree / EWPE Smart OEM** in the pump step. Discover the module on the home network or enter its IPv4 address, device ID and V1/V2. Select **Pair Gree** and wait for a fresh state read. The same controls are available in Settings. The pump's reported indoor temperature can serve as the room temperature input when it is available.

Lite sends bounded UDP requests on port 7000 on the local subnet. It distinguishes V1 AES-ECB and V2 AES-GCM at discovery, matches the selected module and completes a read using the module's device key before saving the enrollment in NVS. The key is local to the device and never returned to the browser. Changing networks invalidates the old read; polling resumes when the configured module is reachable.

The state includes power, operating mode, Celsius setpoint, and indoor temperature or frost mode when the module supplies them. Unknown fields stay empty. The receive time describes the paired LAN response. No independent pump-side timestamp is available. Fahrenheit and proprietary extensions have no setpoint interpretation in this revision.

## Evidence

- [Gree remote wire description](https://github.com/tomikaa87/gree-remote/tree/828c9ec77f25d4475cfca63dd5eb81128dbc24a0) covers V1 scan, bind and state frames.
- [greeclimate source](https://github.com/cmroche/greeclimate/tree/ec124ec8b86a9c9dcf61a783609e57964408c31c) also covers V2 AES-GCM. The implementation and tests in Lite are independent.
- The host protocol suite uses known ciphertext vectors, tests both pairing/read exchanges, rejects changed tags, wrong device IDs, command replies in place of reads, duplicate fields, invalid lengths and ambiguous temperature units. Configure CMake with host OpenSSL and cJSON development headers to run it.
- Physical compatibility with a Gree, Toyotomi or other EWPE Smart model is pending. At this revision the selectable ESP32 driver reads state and shows it in the setup and Settings views. The protocol's command codec is host-tested; device writes remain disabled.
