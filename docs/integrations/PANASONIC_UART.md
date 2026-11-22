# Panasonic CN-CNT and CN-WLAN air-to-air buses

Lite uses UART1 on GPIO17 (TX) and GPIO18 (RX), at 9600 baud with even parity and one stop bit. Use the exact indoor unit's connector wiring with a compatible ready-made interface and check the levels before connecting. The CZ-TACG1 connection reference covers CN-CNT. The DNSK-P11 connection reference covers CN-WLAN. Panasonic Aquarea and HeishaMon are separate integrations.

Setup first tests the CN-CNT status protocol. If it cannot identify that protocol, it tries the CN-WLAN session handshake. The latter can take about 20 seconds. Once connected, the dashboard displays the family, power, mode, setpoint and available temperature readings. Unavailable values stay empty.

With the exact heating model and its range approved, Lite supports HEAT, OFF and half-degree Celsius setpoints within 16–30 °C and the approved range. CN-CNT starts with a fresh read and copies its existing control fields before changing the requested settings. CN-WLAN writes only the requested keys, acknowledges local reports and verifies state using a new poll with its matching sequence. Detected defrost states block writes. The pump's local protections continue to run.

Host tests cover both frame grammars, packet checksums, sequence rollover, setpoint and result parsing, defrost, selective writes and stale WLAN replies. The CN-WLAN handshake and both families' control and reconnect paths still need testing on actual indoor units.

## Sources

- [DomiStyle Panasonic AC at c56eb4f](https://github.com/DomiStyle/esphome-panasonic-ac/tree/c56eb4f85ad93cfbe0e89aee72b646740ee103ae), MIT. The two connector guides, wire constants, frame format and handshake. No ESPHome runtime is linked.
