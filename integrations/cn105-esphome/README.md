# CN105 ESPHome UART measurements

This overlay targets [CN105 2026.5.2](https://github.com/echavet/MitsubishiCN105ESPHome/tree/6b997ec98bc37797909f9b50438c8bb480f7a631) and ESPHome 2026.10.0 on ESP32. It emits the optional Enevina Lite SSE text sensor from the bridge's existing UART traffic. The overlay retains the upstream BSD-2-Clause notices and does not change command scheduling or add UART writes.

The observer starts with the driver's regular poll and follows its actual 0x02, 0x03 and 0x06 request transmissions. A frame is eligible only while its matching response is pending and when the header, length, checksum, values and source clock pass validation. An incomplete, duplicate or unexpected response discards the unfinished sample. Each completed cycle gets a new sequence only after the boot epoch has been saved and read back from NVS. A UART disconnect or discarded poll invalidates the last publication. The consumer still requires consecutive fresh reads across reconnects. CN105 does not carry a request identifier, so this is source freshness on a trusted local bridge, not cryptographic attestation of the serial bus.

## Build in isolation

```sh
git clone --branch 2026.5.2 --single-branch https://github.com/echavet/MitsubishiCN105ESPHome.git upstream
python tools/prepare_cn105_esphome.py upstream
cp integrations/cn105-esphome/cn105-bench.yaml ./cn105-bench.yaml
esphome compile cn105-bench.yaml
```

The fixture is a build configuration. For a hardware bench, supply your local ESPHome configuration, including a synchronized system clock and the existing WebServer v1 for the legacy SSE entity ID. Use the configured entity name exactly as above.

## Field acceptance

Before replacing a working bridge, identify its running build, recover its actual YAML and pins, and verify a restorable image and procedure on an equivalent board. Bench-check checksum rejection, unplug/reconnect, reboot with NVS preserved and both unchanged and changed readings on the SSE wire. Confirm that Lite accepts the new samples and that ordinary climate and command readback behavior still works. Use the pump's natural cycles for field observations.
