#pragma once
#include <cstddef>
#include <cstdint>
namespace enevina::cottage {
// Decodes Ruuvi Data Format 5 manufacturer's raw bytes only.
// Caller must verify BLE address; MAC alone is not authenticated.
bool decode_ruuvi_rawv5_temperature(const uint8_t* manufacturer_data,
                                    size_t count,double& temperature_c);
}
