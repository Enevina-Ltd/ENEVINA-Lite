# Lite N16R8 physical RAM and onboarding test — 23 November 2026

## Hardware / scope

ESP32-S3 revision 0.2, 16 MiB flash, 8 MiB PSRAM, development test card
connected to Core over USB. Selected heat pump: a configured Midea AC V3.
No CN105 bridge, Shelly meter, or indoor-bound RuuviTag. Automatic heating
control remained **disarmed** throughout. No heating command was sent.

The complete 16 MiB flash was backed up locally before USB staging, including
A/B firmware, NVS and the OTA selector. New builds were written only to the
test card's app0 slot; the original confirmed app1 and NVS were not flashed.

## Observed results

| Check | Before lazy optional readers | After |
| --- | --- | --- |
| Free internal heap | approximately 63 KiB | 79,200 bytes initially; 77,076 bytes later |
| Unselected CN105 reader | allocated 8 KiB task stack | not started |
| Unconfigured Shelly meter reader | allocated 5 KiB task stack | not started |
| Midea V3 read | fresh and configured | still fresh and configured |
| MET Norway forecast | no valid points on the test card | 56 verified forecast points; weather API valid |
| Photon city search | OFFLINE, TLS handshake error | Oulu returned 5 results, correct city match |
| Control authorization | disarmed | disarmed |

Previous test images using different Photon trust anchors did **not** solve
city search. The measured memory recovery, with the original, trusted public
certificate restored, made the TLS request succeed. We did **not** weaken
TLS certificate checking, reduce memory gates, or introduce watchdogs.

The signed OTA reader detected a correctly signed test manifest with version
2026112302 but did not begin downloading its image (zero bytes, status 3).
A later free-internal reading was 74,980 bytes, below the 76 KiB image
budget, consistent with the outstanding memory issue; this telemetry does
not isolate the exact lock-versus-heap denial. The prior signed release was
restored to the restricted local distribution server after this test. The
old confirmed application remains available.

## Remaining release gates

- Signed OTA image download, new-bank boot confirmation and rollback on the
  physical card are **not yet proved** under continuous BLE/TLS load.
- OTA issue #47 remains open; compilation and manifest acceptance alone do
  not establish customer-ready update safety.
- Blank N16R8 first installation, other pump adapters and extended unattended
  automatic heating must be verified before customer rollout.
