#pragma once
#include <cstddef>
#include <cstdint>
#include "forecast/fmi.hpp"

namespace enevina_firmware {
struct WeatherStatus {
    bool configured=false;
    bool available=false;
    double next_temperature_c=0;
    int64_t next_utc=0;
    int64_t valid_until_utc=0;
    int64_t fetched_utc=0;
    size_t points=0;
};
// Uses persisted site coordinates rather than a hard-coded firmware location.
// nullptr keeps compatibility with standalone firmware build defaults.
void start_weather_reader(const char* configured_latlon=nullptr);
uint32_t weather_reader_stack_free();
// A validated settings change is applied without restarting ESP32. The
// previous forecast is immediately invalidated and the worker is awakened.
bool weather_update_location(const char* configured_latlon);
WeatherStatus weather_status();
size_t future_weather(enevina::OutdoorPoint* output,size_t capacity);
}
