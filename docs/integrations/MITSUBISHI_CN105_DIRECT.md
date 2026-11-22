# Mitsubishi Electric direct CN105

This option reads the Mitsubishi Electric indoor unit directly through its CN105 UART connection. Choose **Mitsubishi Electric direct CN105** in setup, select the documented baud rate for the unit and run the connection test. Save the verified connection with **Continue**. Mitsubishi Heavy Industries WF-RAC and the Mitsubishi Electric ESPHome bridge remain separate options.

The ESP32-S3 N16R8 build uses UART1 with GPIO17 as TX and GPIO18 as RX. Use a ready-made CN105-compatible UART interface whose connector, signal levels and power rating match the unit and ESP32 board. The N16R8 memory designation covers multiple board layouts. Verify the board's exposed GPIOs and the indoor unit's connector before wiring. The serial format is 8E1 at 2400 or 9600 baud.

Lite verifies the standard heat pump handshake and accepts a settings response after its own request and checksum check. Mode, power and the setpoint are reported when the unit supplies them. Separate requests fetch optional room, outdoor and compressor readings. Missing measurements remain empty. A successful direct temperature request can supply the common room-temperature input.

The native tests cover framing, checksum rejection, both standard temperature encodings and selective command construction. Firmware reads status during this stage. Direct physical compatibility and writes still need bench verification. Use the [ESPHome connector and UART reference](https://esphome.io/components/climate/mitsubishi_cn105/) and [pinned source reference](https://github.com/echavet/MitsubishiCN105ESPHome/tree/b24e4285d901d0c20ab16b1d112fdf9f2851d7b9).
