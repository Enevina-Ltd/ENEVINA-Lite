# Mitsubishi Heavy Industries WF-RAC

Select Mitsubishi Heavy Industries WF-RAC in setup and enter the module's IPv4 address. Use the address on the same home network as Lite. Lite tests the module identity and verifies the binary control-status frame before making the proposed connection available to save. A failed or skipped draft leaves the previous connection in place.

This implementation reads the original HTTP branch on TCP port 51443. The WF-RAC-HTTPS and WCBN4612L branches use TLS and require separate transport verification. Mitsubishi Electric uses the separate CN105 protocol.

Lite reads the requested power state, mode and setpoint, plus temperatures when the validated response carries them. The room temperature is the reading the controller is using. Its absolute value may reflect an external sensor configured on the heat pump, and Lite has no independent evidence of that source. Select RuuviTag when you need a measurement from a specific room. An unknown value remains empty. The pump does not provide a timestamp for the reading.

To configure control, enter the heat pump model, its documented heating range and an operator ID that is already registered on the module. Approve the model-specific trial in Settings or the setup summary. Lite does not register any new operator, show the ID in status or retain it after restart. The original module uses plain HTTP on the home network, including during the write. Approval is bound to the exact module, reported firmware and Wi-Fi connection.

The manual buttons send HEAT, OFF or a half-degree setpoint change in HEAT mode. Automatic control uses the same worker and heating policy. Lite reads the current settings before sending any command, skips runs when the module reports a fault or self-cleaning, and preserves other settings using the protocol's selective field flags. The dashboard distinguishes queued, dispatch started and confirmed. Confirmation comes from a separate fresh read. The compressor flag describes this indoor unit's demand, including on multi-split installations.

The host suite verifies legacy status framing, corruption handling, selective HEAT/OFF frames and mismatched readback. Physical compatibility and the HTTPS module branches still require verification. See the [WF-RAC module reference](https://github.com/blues-sechseck/Mitsubishi-WF-RAC-Integration/blob/4acf985ccc8ce2859c7322ccbfa6ff4bbe818174/docs/wf-rac-module-reference.md) and the [pywfrac source revision](https://github.com/blues-sechseck/pywfrac/tree/39a5568901ed6897ba1045ab2b0453d7bc8c53e0).
