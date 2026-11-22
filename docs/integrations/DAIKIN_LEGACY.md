# Daikin legacy local HTTP

Select the Daikin adapter and enter the module's IPv4 address. **Pair Daikin** reads the module identity and the actual legacy air-conditioner state. **Continue** confirms the proposed connection. A failed or skipped proposal leaves the active pump in place.

Lite uses the module identity and the result of `/aircon/get_control_info` to establish this protocol. Modules that answer only `/common/basic_info` remain unsupported. BRP069C revisions and firmware have mixed compatibility. The DSIoT API used by some newer modules needs a separate implementation.

The native reader polls the selected module in the home network and checks its saved MAC before accepting new state. It uses optional room, outdoor and compressor readings when present. The receive age describes the LAN response. Unknown readings stay empty. There are no cloud requests.

The protocol tests check legacy fields, unknown values, malformed replies, mode-specific defaults and selective command parameters. The ESP32 connection is read-only in this integration slice. A physical compatibility test is pending. The [Apollon77 reference](https://github.com/Apollon77/daikin-controller/tree/6d6d23eae45d57d04dff2ab811a30f81bce4093f) and [pydaikin reference](https://github.com/fredrike/pydaikin/tree/3c88d3fe1ccd64e94c7d54ce52643f2c44cf6058) describe the source projects' support.
