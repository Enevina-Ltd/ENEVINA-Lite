#pragma once
// Firmware has NO actuator transport. Configuration never enables physical writes.
// Copy optional cottage_local_config.hpp in this directory, outside source control.
// NEVER commit passwords, device addresses or independent BLE sensor identifiers.
#if __has_include("cottage_local_config.hpp")
#include "cottage_local_config.hpp"
#endif
#ifndef COTTAGE_WIFI_SSID
#define COTTAGE_WIFI_SSID ""
#endif
#ifndef COTTAGE_WIFI_PASSWORD
#define COTTAGE_WIFI_PASSWORD ""
#endif
#ifndef COTTAGE_CN105_URL
#define COTTAGE_CN105_URL ""
#endif
#ifndef COTTAGE_SHELLY_URL
#define COTTAGE_SHELLY_URL ""
#endif
#ifndef COTTAGE_RUUVI_MAC
#define COTTAGE_RUUVI_MAC ""
#endif
#ifndef COTTAGE_FMI_LATLON
#define COTTAGE_FMI_LATLON ""
#endif
#ifndef COTTAGE_SPOT_MARGIN
#define COTTAGE_SPOT_MARGIN 0.0
#endif
#ifndef COTTAGE_TRANSFER_DAY
#define COTTAGE_TRANSFER_DAY 0.0
#endif
#ifndef COTTAGE_TRANSFER_NIGHT
#define COTTAGE_TRANSFER_NIGHT 0.0
#endif
#ifndef COTTAGE_ELECTRICITY_TAX
#define COTTAGE_ELECTRICITY_TAX 0.0
#endif
#ifndef COTTAGE_VAT_RATE
#define COTTAGE_VAT_RATE 0.255
#endif
// Test-only synthetic physics: NEVER trust for C3 or physical heating.
#ifndef COTTAGE_SYNTHETIC_TEST_MODEL
#define COTTAGE_SYNTHETIC_TEST_MODEL 0
#endif
