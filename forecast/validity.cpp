#include "forecast/validity.hpp"
#include "optimizer/optimizer.hpp"
#include <cmath>

namespace enevina {
ForecastStatus inspect_forecast(const ForecastSlot* slots, size_t available,
    int64_t now_utc, int64_t price_fetched_utc, int64_t weather_fetched_utc,
    size_t required_slots, int64_t maximum_price_age_s,
    int64_t maximum_weather_age_s) {
    if (now_utc <= 0 || price_fetched_utc > now_utc ||
        weather_fetched_utc > now_utc || maximum_price_age_s <= 0 ||
        maximum_weather_age_s <= 0) return ForecastStatus::ClockError;
    if (!slots || required_slots == 0 || required_slots > max_periods ||
        available < required_slots || price_fetched_utc <= 0 ||
        weather_fetched_utc <= 0) return ForecastStatus::Missing;
    if (now_utc-price_fetched_utc > maximum_price_age_s)
        return ForecastStatus::StalePrice;
    if (now_utc-weather_fetched_utc > maximum_weather_age_s)
        return ForecastStatus::StaleWeather;
    const int64_t current_start=now_utc-(now_utc%900);
    for (size_t i=0;i<required_slots;++i) {
        if (slots[i].start_utc != current_start+static_cast<int64_t>(i)*900 ||
            !std::isfinite(slots[i].total_price_eur_per_kwh) ||
            !std::isfinite(slots[i].outdoor_c)) return ForecastStatus::Gap;
    }
    return ForecastStatus::Ready;
}
}
