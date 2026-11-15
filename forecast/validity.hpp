#pragma once
#include <cstddef>
#include <cstdint>

namespace enevina {
struct ForecastSlot {
    int64_t start_utc; // Unix seconds, never local wall time
    double total_price_eur_per_kwh;
    double outdoor_c;
};
enum class ForecastStatus { Ready, Missing, Gap, StalePrice, StaleWeather, ClockError };
ForecastStatus inspect_forecast(const ForecastSlot* slots, size_t available,
    int64_t now_utc, int64_t price_fetched_utc, int64_t weather_fetched_utc,
    size_t required_slots, int64_t maximum_price_age_s=36*3600,
    int64_t maximum_weather_age_s=6*3600);
}
