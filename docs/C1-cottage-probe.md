# C1 read-only network evidence — 2026-11-16

**Observation only. No device writes, switching, configuration changes or flashing.**
The address values were provided to the probe **at runtime**, never checked into Git.

## Mitsubishi FD25VA CN105

A local ESP32-S3 CN105 bridge returned HTTP 200 from its `/events`
Server-Sent Events endpoint, content-type `text/event-stream`.
Its live initial snapshot contained:

- `sensor/Compressor Frequency` value **0 Hz**
- `climate/Mitsubishi Electric MSZ-FD25VA`: `mode=OFF`,
  `action=OFF`, `current_temperature="12.0"`,
  `target_temperature="21.0"` and minimum device setpoint `16.0`.
- Other fields were available (unit uptime, error code), but their
  freshness and validity require continuous checking.

Important: the **21 °C** target displayed while OFF is a retained pump
setting. It does **not** invalidate the requirement that simulated future
HEAT commands, if ever approved, use **16 °C**. No HEAT commands were sent.

## Shelly Plus 1PM

Read-only local RPC calls `Shelly.GetDeviceInfo` and
`Switch.GetStatus?id=0` returned HTTP 200 and identified a Gen 2
**Shelly Plus 1PM**. At the instant sampled:

- Relay reported `output=true`, never modified
- Whole-cottage electric power: **165.8 W**
- Cumulative energy: **1,890,100.907 Wh**

The result cannot identify the heat pump's standalone electrical
consumption. It is not a COP calibration datum.

## Reproduction and unresolved checks

```sh
python3 tools/cottage_probe.py \
  --cn105-url "http://LOCAL_CN105_HOST/events" \
  --shelly-url "http://LOCAL_SHELLY_HOST" --timeout 6
```

This command uses **GET only** and emits no relay/heat pump commands.
The probe handles the actual SSE `event: state` JSON schema and numeric
strings found in this firmware version. It is a **PC diagnostic**, not the
final nonblocking embedded transport.

Still unresolved: independent air-temperature sensor in the coldest
protected part of the cottage; ESP32 pin-level electrical validation;
end-to-end ESP-IDF **Wi-Fi** shadow integration, scheduler synchronization
and simultaneous task performance; valid HEAT 16/OFF command ACK and
compressor minimum run/stop response; power failures; watchdog/24–48h
soak; independent physical frost protection.

A single successful GET pair never establishes availability for future
hours. C3 actuation remains disabled.
