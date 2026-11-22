# Haier hOn and SmartAir2

Lite talks directly to a compatible indoor unit through its local Wi-Fi-module connector using a model-appropriate adapter. The selected connection uses Lite UART1 on GPIO17 (TX) and GPIO18 (RX), at 9600 baud, 8N1. Follow the [ESPHome electrical reference](https://esphome.io/components/climate/haier/) and verify connector pinout and voltage for the exact unit before wiring. Lite shares this UART with its other direct bus adapters.

The setup test distinguishes hOn from SmartAir2 using the version exchange and a valid status reply. hOn negotiates CRC, queries device information, and currently accepts the documented layout with ten control bytes, 22 sensor bytes and zero extra header bytes. SmartAir2 uses the 24-byte status layout. Other hOn packet layouts require model-specific validation. A supported bus reply, rather than the name of a retail application, determines whether setup can continue.

The dashboard shows the detected family, power, mode, setpoint and available temperature measurements. SmartAir2 has no outdoor value in the supported status packet. With a model-specific Celsius range approved, Lite can request HEAT, OFF and whole-degree setpoints from 16 to 30 °C within the approved range. Existing half-degree status remains readable. The driver retains unrelated settings from a fresh pre-read and confirms a write only with a new status query. A write response does not serve as confirmation. hOn cooling-only, fault and cleaning status and both families' ten-degree and Fahrenheit modes prevent writes. The pump keeps responsibility for defrost and local protection.

The host tests exercise both wire layouts, escaping, checksum, CRC, false confirmation, external changes and selective writes. Model-specific heating, reconnect and readback on a physical Haier unit remain to be checked.

## Sources

- [HaierProtocol at d3334a1](https://github.com/paveldn/HaierProtocol/tree/d3334a1be24da142dd7fb68c6d2abfd628bfaded), MIT. Transport description and simulator behaviour for hOn and SmartAir2.
- [ESPHome Haier at 1feda1b](https://github.com/esphome/esphome/tree/1feda1bcaa861a5f2e933f2c65d599e44f4b132b/esphome/components/haier), GPL-3.0. Wire layout and model-dependent options. Lite implements the described protocol in C++ without either runtime.
