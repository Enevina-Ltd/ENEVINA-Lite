#include "cottage/forecast_bridge.hpp"
#include "cottage/finnish_tariff.hpp"
#include <cmath>
#include <ctime>
namespace enevina::cottage {
BridgeResult join_forecast_two_tariffs(int64_t now,int64_t price_fetch,int64_t weather_fetch,
    const Spot15* prices,size_t n,const enevina::OutdoorPoint* weather,size_t m,
    const enevina::Tariff& day_tariff,const enevina::Tariff& night_tariff,size_t slots) {
    BridgeResult result;
    if(now<1700000000) {result.status=BridgeStatus::BadClock;return result;}
    if(!prices||!weather||!n||n>210||m<2||m>64||slots<1||slots>192)
        return result;
    if(price_fetch>now || price_fetch<now-36*3600) {
        result.status=BridgeStatus::StalePrices;return result;
    }
    if(weather_fetch>now||weather_fetch<now-6*3600) {
        result.status=BridgeStatus::StaleWeather;return result;
    }
    // Always plan at a UTC quarter-hour boundary; caller triggers after boundary.
    const int64_t start=((now+899)/900)*900;
    for(size_t i=0;i<slots;++i) {
        const int64_t ts=start+static_cast<int64_t>(i)*900;
        double spot=0;
        bool found=false;
        for(size_t j=0;j<n;++j) {
            if(prices[j].utc==ts) {
                if(found) {result.status=BridgeStatus::Gap;return result;}
                spot=prices[j].eur_per_kwh;found=true;
            }
        }
        if(!found) {result.status=BridgeStatus::Gap;return result;}
        bool day=false;
        if(!finnish_day_transfer(ts,day)) {
            result.status=BridgeStatus::BadClock;return result;
        }
        enevina::Tariff t=day?day_tariff:night_tariff;
        t.spot_eur_per_kwh=spot;
        double total=0;
        if(!enevina::calculate_total_price(t,total)) {
            result.status=BridgeStatus::BadPrice;return result;
        }
        result.periods[i].total_price_eur_per_kwh=total;
        result.periods[i].internal_gain_kw=0;
        result.periods[i].defrost_capacity_factor=1.0; // Nominal, uncertainty not validated.
    }
    std::array<double,192> outdoor{};
    if(!enevina::interpolate_fmi_temperature(weather,m,start,
        outdoor.data(),slots)) {
        result.status=BridgeStatus::BadWeather;return result;
    }
    for(size_t i=0;i<slots;++i) {
        if(!std::isfinite(outdoor[i]) || outdoor[i]<-55 || outdoor[i]>45) {
            result.status=BridgeStatus::BadWeather;return result;
        }
        result.periods[i].outdoor_c=outdoor[i];
    }
    result.status=BridgeStatus::Ready;
    result.count=slots;
    result.first_utc=start;
    return result;
}
BridgeResult join_forecast(int64_t now,int64_t price_fetch,int64_t weather_fetch,
    const Spot15* prices,size_t n,const enevina::OutdoorPoint* weather,size_t m,
    const enevina::Tariff& tariff,size_t slots) {
    return join_forecast_two_tariffs(now,price_fetch,weather_fetch,
        prices,n,weather,m,tariff,tariff,slots);
}
} // namespace enevina::cottage
