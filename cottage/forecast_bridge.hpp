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
    BadWeather, Invalid, UnalignedStart
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
// Generic form: `pricer` returns the TOTAL EUR/kWh for the slot starting at
// `utc` (e.g. site_total_price). Used by the firmware so the MPC bridge, the
// price bootstrap and the dashboard share ONE price rule.
using SlotPricer=bool(*)(void* context,int64_t utc,double spot_eur_per_kwh,double& total);
BridgeResult join_forecast_priced(int64_t now_utc,int64_t price_fetch_utc,
    int64_t weather_fetch_utc,const Spot15* spot,size_t spot_count,
    const enevina::OutdoorPoint* weather,size_t weather_count,
    SlotPricer pricer,void* context,size_t slots);
// Receding-horizon form used by the live MPC. Instead of requiring a fixed
// 48 h or 24 h block, it returns the LONGEST horizon that is backed by
// verified data, capped at `max_slots` (192 = 48 h), and fails only when
// fewer than `min_slots` remain:
//  * prices: strictly contiguous UTC 15-minute slots from the current slot;
//    the first missing, duplicated or unpriceable slot ends the horizon.
//    Negative spot prices are valid inputs. The tariff/VAT/currency/local
//    time-zone rule is the caller's `pricer`, evaluated per slot start, so
//    DST days (92/100 slots) need no special case.
//  * weather: the strictly hourly run of MET Norway points that covers the
//    current slot; a missing hour ends the horizon there (no gap filling).
//  * stale inputs (prices > 36 h, weather > 6 h old, or from the future)
//    are unusable, exactly like join_forecast_priced().
enum class HorizonLimit : uint8_t { None, MaxSlots, PriceEnd, PriceGap, BadPrice,
    WeatherEnd, WeatherGap, LearnedOutdoorRange };
struct HorizonResult {
    BridgeResult bridge{};
    size_t price_slots=0;    // contiguous priced slots available
    size_t weather_slots=0;  // slots covered by contiguous hourly weather
    HorizonLimit limit=HorizonLimit::None;
};
// In-place variant: the caller can keep the large period array in PSRAM.
// Reinitializes the complete output, including on failure and reuse.
void join_forecast_horizon_into(HorizonResult& out,int64_t now_utc,int64_t price_fetch_utc,
    int64_t weather_fetch_utc,const Spot15* spot,size_t spot_count,
    const enevina::OutdoorPoint* weather,size_t weather_count,
    SlotPricer pricer,void* context,size_t max_slots,size_t min_slots);
HorizonResult join_forecast_horizon(int64_t now_utc,int64_t price_fetch_utc,
    int64_t weather_fetch_utc,const Spot15* spot,size_t spot_count,
    const enevina::OutdoorPoint* weather,size_t weather_count,
    SlotPricer pricer,void* context,size_t max_slots,size_t min_slots);
const char* bridge_status_name(BridgeStatus s);
const char* horizon_limit_name(HorizonLimit l);
} // namespace enevina::cottage
