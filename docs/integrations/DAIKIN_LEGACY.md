# Daikin legacy local HTTP

Select the Daikin adapter and enter the module's IPv4 address. **Pair Daikin** reads the module identity and the actual legacy air-conditioner state. **Continue** confirms the proposed connection. A failed or skipped proposal leaves the active pump in place.

Lite uses the module identity and the result of `/aircon/get_control_info` to establish this protocol. Modules that answer only `/common/basic_info` remain unsupported. BRP069C revisions and firmware have mixed compatibility. The DSIoT API used by some newer modules needs a separate implementation.

The native reader polls the selected module in the home network and checks its saved MAC before accepting new state. It uses optional room, outdoor and compressor readings when present. The receive age describes the LAN response. Unknown readings stay empty. There are no cloud requests.

After successful pairing, enter the heat pump model and the heating range and step from its manual. Approve the model-specific control trial under Settings or in the setup summary. Approval is held in RAM and applies to the current Wi-Fi connection and module identity, including its reported firmware. Automatic control can then be armed from the same screen. When automatic control is off, Settings offers HEAT, OFF and a setpoint change in HEAT mode. No external temperature sensor is needed for these manual commands.

The same worker serializes both control paths. It reads the paired module again before a command, preserves unrelated control fields, applies the 30-minute operating-mode dwell, and checks a new request for the exact result. A changed preflight state cancels the automatic proposal and updates the next plan. The dashboard distinguishes requested, dispatch started and confirmed values. The legacy API does not provide a pump-origin timestamp or a verified defrost signal. Lite uses only ordinary climate commands and leaves defrost and other protections to the pump.

The protocol tests cover model-specific setpoint grids, stale and mismatched readback, unknown values, malformed replies, mode-specific defaults and command fields. A physical compatibility test is pending. The [Apollon77 reference](https://github.com/Apollon77/daikin-controller/tree/6d6d23eae45d57d04dff2ab811a30f81bce4093f) and [pydaikin reference](https://github.com/fredrike/pydaikin/tree/3c88d3fe1ccd64e94c7d54ce52643f2c44cf6058) describe the source projects' support.
