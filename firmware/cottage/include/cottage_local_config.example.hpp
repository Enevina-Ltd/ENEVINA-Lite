// Rename to cottage_local_config.hpp and keep it out of Git.
// No flash or live device modifications are required to run this example build.
#pragma once
#define COTTAGE_WIFI_SSID "your-2.4ghz-network"
#define COTTAGE_WIFI_PASSWORD "your-wifi-password"
#define COTTAGE_CN105_URL "http://CN105_IP/events"
#define COTTAGE_SHELLY_URL "http://SHELLY_IP"
// Optional explicit RAWv2 MAC. Leave empty to discover and bind via LAN UI.
// When Wi-Fi is unavailable, binding from this private header still allows
// standalone local BLE+humidity monitoring.
// Never commit real sensor identifiers.
#define COTTAGE_RUUVI_MAC ""
#define COTTAGE_OUTDOOR_RUUVI_MAC "" // optional second sensor with RH
#define COTTAGE_FMI_LATLON "61.4,24.1"  // Example coordinates, replace
#define COTTAGE_SPOT_MARGIN 0.0
#define COTTAGE_TRANSFER_DAY 0.0
#define COTTAGE_TRANSFER_NIGHT 0.0
#define COTTAGE_ELECTRICITY_TAX 0.0
#define COTTAGE_VAT_RATE 0.255
// #define COTTAGE_SYNTHETIC_TEST_MODEL 1  // bench only, never production
