# Cottage Lite ESP32-S3 lost LAN access after installation — 23 November 2026

## Physical observations

A previously USB-updated N16R8 cottage controller booted app1 VALID and
initially rejoined the saved home Wi-Fi. During the observed interval it had
RSSI about -37 dBm, a fresh bound RuuviTag (around 7.3 C), fresh MET Norway
forecast, Nord Pool prices, Shelly power reads and a live CN105 SSE socket.
Heating writes remained disarmed.

The controller became unreachable from the Core machine on its last observed
LAN address. Both HTTP and ARP stopped responding. The Mitsubishi CN105
ESPHome bridge was still reachable on the same local network and returned
HEAT-PUMP OFF state. Further checks from multiple machines did not reach Lite.
The board is now at the cottage rather than on the local USB test bench.

**The root trigger is not proven.** A physical device can lose power, reset,
be disassociated by an access point or stop servicing the Wi-Fi/IP stack.
A prior -37 dBm RSSI value does not prove the radio stayed healthy after
the last sample. The actual Wi-Fi disconnect event and boot log were not
retrievable while the board remained offline.

## Source-level defect confirmed

The firmware used WiFi.setAutoReconnect(true) without a separate bounded
retry after an unexpected disconnection. The exact bundled Arduino-ESP32
2.0.17 library only automatically retries listed disconnect reasons and its
first-connection fallback does not cover every subsequent disassociation.
The Wi-Fi onboarding state machine opened a recovery setup AP after five
minutes but did not itself reattempt the saved home network.

### Firmware change

- WifiLinkRetry retries the *already saved* Wi-Fi association at most once
  per 30 seconds if the state is RECONNECTING/RECOVERY and there is no IP.
  On reconnect, its delay resets. It neither mutates NVS nor interferes with
  new-network setup; no reboot or new watchdog is introduced.
- /api/wifi/status now reports disconnect count, last reason code,
  last disconnect age, bounded reconnect attempts/accepted requests and
  current station-IP state. It does not reveal Wi-Fi credentials, BSSIDs,
  private keys or pump authorization.
- Existing setup-AP restrictions and thermostat/heat-command gates are
  unchanged. Do not force heating or flash an offline controller remotely.

## Verification scope

Source regressions and host-side Wi-Fi retry state-machine checks cover
start-up without credentials, a lost saved station, bounded repeat attempts,
restoration, setup wizard exclusion and timestamp wraparound. ESP32 firmware
compilation validates firmware integration. A real disconnection/recovery
exercise with a radio/client event capture is still required to confirm
this was the cottage's original failure mode.

When the board next reconnects, collect read-only /api/status
boot_reset_reason_code and /api/wifi/status diagnostics before any reset.
The reason code alone does not prove a power or AP fault; correlate it with
boot uptime, connection reason and router/AP logs.
