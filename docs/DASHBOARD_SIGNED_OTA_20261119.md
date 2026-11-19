# Enevina Lite – firmware update from the dashboard

**Status: implemented in source and ESP32-S3 firmware build; physical device test pending.**

## Device user experience

In **Settings > Firmwarepäivitys**, Lite displays the currently running firmware,
the last confirmed signed OTA release, the last publisher manifest version checked,
the latest check time, update progress, and the active/reported status.

Click **Tarkista ja asenna päivitys**, then accept the browser's explicit
confirmation. The device immediately schedules its existing background signed
HTTPS OTA worker. It applies a *new eligible publisher-signed release* when one
has been staged on the previously configured channel, and reboots to the
inactive OTA slot only after full size, SHA-256 and signature checks.

The existing recovery logic confirms the new image only after boot health checks.
The previous working partition remains available for rollback. There are no new
device passwords, PINs, accounts, or manual USB updates on each subsequent
signed firmware release.

**No file-upload endpoint has been added.** This dashboard control downloads
only releases signed by the channel publisher. Dragging an arbitrary
`firmware.bin` into the browser or publishing unsigned GitHub CI builds will
not work. This intentionally preserves the existing secure OTA trust chain.

## Integration, security and failure behavior

- `POST /api/ota/install` reuses the existing CSRF/local-settings token
  returned via the device's status endpoint. The endpoint refuses open Wi-Fi
  provisioning AP requests, disconnected LAN requests, unconfigured channels,
  pending boot confirmation and active OTA maintenance.
- A successful `202 {"queued":true}` only means the background worker was
  awakened; it is **not** a promise that a newer release exists or was
  installed. The dashboard must display progress and the subsequent result.
- The worker retains its initial 90-second boot stabilization interval.
  Requests made before that are queued. Existing 60-second scheduled checks
  and staged-release behavior are preserved; the button adds on-demand
  triggering, not another flashing implementation.
- Existing trusted CA, ECDSA P-256 publisher signature, manifest version
  monotonicity/quarantine, target model, image size, SHA-256, OTA partition,
  rollback and health check remain unchanged.
- Firmware image streaming and boot partition writes are performed only by
  the existing signed worker, never by the WebServer request callback.
- No credentials, Midea pairing secrets, or heating controls are changed.

## Preconditions and test limitations

1. The device must already have a compatible A/B OTA bootloader/partition
   layout and trust anchors installed by the approved initial provisioning.
2. The local signed-release HTTPS server must be available and carry a new,
   eligible, channel-signed firmware release.
3. If the device was built without the private channel configuration header,
   `ota_signed_pull_enabled=false` and the dashboard button remains disabled
   with an explanation. An ordinary unsigned `firmware.bin` alone is not
   enough to create a secure release.
4. The physical ESP32-S3 deployment and its exact partition/rollback state
   must be checked before testing on real hardware. The F02 card's previous
   full-flash backups failed, so this development does **not** authorize
   flashing that card or modifying Core/heat-pump production settings.

## Testing

- Dashboard translation coverage: `python3 -m unittest discover -s tests -p test_dashboard_translations.py -v`
- OTA dashboard security/flow: `python3 -m unittest discover -s tests -p test_dashboard_signed_ota.py -v`
- Native build/CTest and `esp32s3_cottage_shadow` PlatformIO build
- Physical signed update/rollback field exercise is **not yet completed**.