# Daikin S21

Select **Daikin S21** and run the connection test in the setup wizard. Lite reads the standard two-character S21 status protocol at 2400 baud, 8E2. Use a ready-made level interface and matching cable for the exact indoor unit. On Lite, UART1 uses GPIO17 for TX and GPIO18 for RX. Check that the ESP32-S3 board exposes those pins. CN_WIRED and other Daikin buses need separate support.

Lite checks the response fields and checksum before saving the connection. Power, mode and the setpoint come from the control response. Room and outdoor temperature appear when the corresponding queries are supported and return usable values. Lite leaves missing measurements empty.

For control, enter the model and the supported heating temperature range, then approve the trial. Manual and automatic commands share the same serial worker and dwell policy. A command preserves the existing fan field and has a new status query for confirmation. Changing the serial protocol changes the bus identity and invalidates the former command approval.

The host suite covers framing, checksums, temperatures, field preservation and matching readback. Physical S21 compatibility is still pending. See the [ESPHome S21 reference](https://github.com/asund/esphome-daikin-s21/tree/1ff429896af20b477903818a515ad2ee25a26e6e) and [Faikout protocol notes](https://codeberg.org/RevK/ESP32-Faikout/src/commit/12b13cc403872c17d9cf00e53b83558a5ec9a81b/Manuals/S21.md).
