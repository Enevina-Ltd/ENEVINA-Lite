# Mitsubishi Heavy Industries WF-RAC

Select Mitsubishi Heavy Industries WF-RAC in setup and enter the module's IPv4 address. Use the address on the same home network as Lite. Lite tests the module identity and verifies the binary control-status frame before making the proposed connection available to save. A failed or skipped draft leaves the previous connection in place.

This implementation reads the original HTTP branch on TCP port 51443. The WF-RAC-HTTPS and WCBN4612L branches use TLS and require separate transport verification. Mitsubishi Electric uses the separate CN105 protocol.

Lite reads the requested power state, mode and setpoint, plus temperatures when the validated response carries them. The room temperature is the reading the controller is using. Its absolute value may reflect an external sensor configured on the heat pump, and Lite has no independent evidence of that source. Select RuuviTag when you need a measurement from a specific room. An unknown value remains empty. The pump does not provide a timestamp for the reading.

At this stage Lite only reads the module. The host suite verifies legacy status framing, corruption handling and the native HEAT/OFF wire format. Manual and automatic commands remain disabled pending integration. Physical compatibility has not been verified. See the [WF-RAC module reference](https://github.com/blues-sechseck/Mitsubishi-WF-RAC-Integration/blob/4acf985ccc8ce2859c7322ccbfa6ff4bbe818174/docs/wf-rac-module-reference.md) and the [pywfrac source revision](https://github.com/blues-sechseck/pywfrac/tree/39a5568901ed6897ba1045ab2b0453d7bc8c53e0).
