#pragma once
#include "cottage/physics.hpp"
#include "forecast/fmi.hpp"
#include "forecast/pricing.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace enevina::cottage {
struct Spot15 {
    int64_t utc=0;
    double eur_per_kwh=0;
};
enum class BridgeStatus : uint8_t {
    Ready, BadClock, StalePrices, StaleWeather, Gap, BadPrice,
    BadWeather, Invalid
};
struct BridgeResult {
    BridgeStatus status=BridgeStatus::Invalid;
    size_t count=0;
    int64_t first_utc=0;
    std::array<Period,192> periods{};
};
BridgeResult join_forecast(int64_t now_utc,int64_t price_fetch_utc,
    int64_t weather_fetch_utc,const Spot15* spot,size_t spot_count,
    const enevina::OutdoorPoint* weather,size_t weather_count,
    const enevina::Tariff& tariff,size_t slots);
// Transfer price applies at the Finnish local 07-22 / 22-07 tariff boundary.
// UTC to Europe/Helsinki DST is calculated independently of the host TZ.
BridgeResult join_forecast_two_tariffs(int64_t now_utc,int64_t price_fetch_utc,
    int64_t weather_fetch_utc,const Spot15* spot,size_t spot_count,
    const enevina::OutdoorPoint* weather,size_t weather_count,
    const enevina::Tariff& day_tariff,const enevina::Tariff& night_tariff,
    size_t slots);
} // namespace enevina::cottage
