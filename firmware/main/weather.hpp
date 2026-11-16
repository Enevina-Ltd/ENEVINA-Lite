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
void start_weather_reader();
WeatherStatus weather_status();
size_t future_weather(enevina::OutdoorPoint* output,size_t capacity);
}
