# Daikin DSIoT

Select **Daikin DSIoT** and enter the Wi-Fi module's IPv4 address on your home network. Lite requires a live adapter identity and a DSIoT indoor climate response. Continue in setup to save the verified module. Lite checks the resulting device identity every time it reads status.

This is a separate protocol from Daikin legacy HTTP. The upstream references describe testing with BRP084 hardware, including BRP084C44 on 3.12.3. Lite decides compatibility using the actual DSIoT responses. BRP069C and other module families have no general support claim.

Lite displays the local module's last reported power, mode, setpoint and optional temperature. Missing fields stay empty. Successful HTTP replies establish module reachability. Until a pump-side freshness signal is established, these readings stay in the status view and room control needs a RuuviTag. Firmware control and physical DSIoT testing are pending. Host tests cover selective HEAT, OFF and setpoint write serialization and exact readback.

See the [Hubitat reference](https://github.com/Mclass294/Hubitat-Daikin-BRP084/tree/0712ecb567608c9c6298f73f96904276f2bd2ce5) and the [pydaikin reference](https://github.com/fredrike/pydaikin/tree/3c88d3fe1ccd64e94c7d54ce52643f2c44cf6058).
