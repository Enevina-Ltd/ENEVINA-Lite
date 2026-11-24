#include "cottage/forecast_bridge.hpp"
#include "cottage/finnish_tariff.hpp"
#include <cmath>
#include <ctime>
namespace enevina::cottage {
namespace {
struct TwoTariffs { const enevina::Tariff* day; const enevina::Tariff* night; bool clock_ok; };
bool two_tariff_price(void* context,int64_t ts,double spot,double& total) {
    auto* t=static_cast<TwoTariffs*>(context);
    bool day=false;
    if(!finnish_day_transfer(ts,day)) {t->clock_ok=false;return false;}
    enevina::Tariff x=day?*t->day:*t->night;
    x.spot_eur_per_kwh=spot;
    return enevina::calculate_total_price(x,total);
}
}
BridgeResult join_forecast_two_tariffs(int64_t now,int64_t price_fetch,int64_t weather_fetch,
    const Spot15* prices,size_t n,const enevina::OutdoorPoint* weather,size_t m,
    const enevina::Tariff& day_tariff,const enevina::Tariff& night_tariff,size_t slots) {
    TwoTariffs context{&day_tariff,&night_tariff,true};
    BridgeResult result=join_forecast_priced(now,price_fetch,weather_fetch,prices,n,
        weather,m,two_tariff_price,&context,slots);
    if(!context.clock_ok) {result=BridgeResult{};result.status=BridgeStatus::BadClock;}
    return result;
}
BridgeResult join_forecast_priced(int64_t now,int64_t price_fetch,int64_t weather_fetch,
    const Spot15* prices,size_t n,const enevina::OutdoorPoint* weather,size_t m,
    SlotPricer pricer,void* context,size_t slots) {
    BridgeResult result;
    if(now<1700000000) {result.status=BridgeStatus::BadClock;return result;}
    if(!prices||!weather||!pricer||!n||n>210||m<2||m>64||slots<1||slots>192)
        return result;
    if(price_fetch>now || price_fetch<now-36*3600) {
        result.status=BridgeStatus::StalePrices;return result;
    }
    if(weather_fetch>now||weather_fetch<now-6*3600) {
        result.status=BridgeStatus::StaleWeather;return result;
    }
    // Always plan at a UTC quarter-hour boundary; caller triggers after boundary.
    // Initial measured temperature is at the current UTC quarter-hour.
    const int64_t start=(now/900)*900;
    // An indoor reading near the END of an interval cannot be projected
    // as if it were measured at the interval START. Fail closed.
    if(now-start>60) {
        result.status=BridgeStatus::UnalignedStart;return result;
    }
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
        double total=0;
        if(!pricer(context,ts,spot,total) || !std::isfinite(total)) {
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
void join_forecast_horizon_into(HorizonResult& out,int64_t now,int64_t price_fetch,int64_t weather_fetch,
    const Spot15* prices,size_t n,const enevina::OutdoorPoint* weather,size_t m,
    SlotPricer pricer,void* context,size_t max_slots,size_t min_slots) {
    // Reset the metadata and periods directly; no 6 KiB aggregate temporary.
    auto clear_bridge=[](BridgeResult& r) {
        r.status=BridgeStatus::Invalid;r.count=0;r.first_utc=0;
        for(auto& period:r.periods)period=Period{};
    };
    BridgeResult& result=out.bridge;
    clear_bridge(result);
    out.price_slots=0;out.weather_slots=0;out.limit=HorizonLimit::None;
    if(now<1700000000) {result.status=BridgeStatus::BadClock;return;}
    if(!prices||!weather||!pricer||!n||n>210||m<2||m>64||
       max_slots<1||max_slots>192||min_slots<1||min_slots>max_slots)
        return;
    if(price_fetch>now || price_fetch<now-36*3600) {
        result.status=BridgeStatus::StalePrices;return;
    }
    if(weather_fetch>now||weather_fetch<now-6*3600) {
        result.status=BridgeStatus::StaleWeather;return;
    }
    const int64_t start=(now/900)*900;
    if(now-start>60) {result.status=BridgeStatus::UnalignedStart;return;}
    // Prices: contiguous prefix only.
    size_t priced=0;
    out.limit=HorizonLimit::MaxSlots;
    for(size_t i=0;i<max_slots;++i) {
        const int64_t ts=start+static_cast<int64_t>(i)*900;
        size_t matches=0;double spot=0;
        for(size_t j=0;j<n;++j) if(prices[j].utc==ts) {spot=prices[j].eur_per_kwh;++matches;}
        if(matches!=1) {out.limit=matches?HorizonLimit::PriceGap:
            (i>0 && ts>prices[n-1].utc?HorizonLimit::PriceEnd:HorizonLimit::PriceGap);break;}
        double total=0;
        if(!std::isfinite(spot) || !pricer(context,ts,spot,total) || !std::isfinite(total)) {
            out.limit=HorizonLimit::BadPrice;break;
        }
        result.periods[i].total_price_eur_per_kwh=total;
        result.periods[i].internal_gain_kw=0;
        result.periods[i].defrost_capacity_factor=1.0;
        priced=i+1;
    }
    out.price_slots=priced;
    // Weather: the hourly run that contains `start`.
    size_t first=m;
    for(size_t j=0;j+1<m;++j)
        if(weather[j].timestamp_utc<=start && weather[j+1].timestamp_utc>start) {first=j;break;}
    size_t last=first;
    if(first<m) {
        while(last+1<m && weather[last+1].timestamp_utc-weather[last].timestamp_utc==3600 &&
              std::isfinite(weather[last+1].temperature_c) &&
              weather[last+1].temperature_c>=-60 && weather[last+1].temperature_c<=60)
            ++last;
        if(!std::isfinite(weather[first].temperature_c) ||
           weather[first+1].timestamp_utc-weather[first].timestamp_utc!=3600) last=first;
    }
    size_t covered=0;
    if(first<m && last>first)
        covered=static_cast<size_t>((weather[last].timestamp_utc-start)/900)+1;
    out.weather_slots=covered;
    size_t count=std::min(priced,covered);
    if(covered<priced)
        out.limit=(last+1<m)?HorizonLimit::WeatherGap:HorizonLimit::WeatherEnd;
    count=std::min(count,max_slots);
    if(count<min_slots) {
        clear_bridge(result);
        result.status=covered<priced?
            (first<m?BridgeStatus::BadWeather:BridgeStatus::StaleWeather):
            (out.limit==HorizonLimit::BadPrice?BridgeStatus::BadPrice:BridgeStatus::Gap);
        return;
    }
    std::array<double,192> outdoor{};
    if(!enevina::interpolate_fmi_temperature(weather+first,last-first+1,start,
        outdoor.data(),count)) {
        clear_bridge(result);result.status=BridgeStatus::BadWeather;return;
    }
    for(size_t i=0;i<count;++i) {
        if(!std::isfinite(outdoor[i]) || outdoor[i]<-55 || outdoor[i]>45) {
            clear_bridge(result);result.status=BridgeStatus::BadWeather;return;
        }
        result.periods[i].outdoor_c=outdoor[i];
    }
    for(size_t i=count;i<result.periods.size();++i) result.periods[i]=Period{};
    result.status=BridgeStatus::Ready;
    result.count=count;
    result.first_utc=start;
    if(count==max_slots) out.limit=HorizonLimit::MaxSlots;
    return;
}
HorizonResult join_forecast_horizon(int64_t now,int64_t price_fetch,int64_t weather_fetch,
    const Spot15* prices,size_t n,const enevina::OutdoorPoint* weather,size_t m,
    SlotPricer pricer,void* context,size_t max_slots,size_t min_slots) {
    HorizonResult out;
    join_forecast_horizon_into(out,now,price_fetch,weather_fetch,prices,n,weather,m,
        pricer,context,max_slots,min_slots);
    return out;
}
const char* bridge_status_name(BridgeStatus s) {
    switch(s) {
    case BridgeStatus::Ready:return "READY";
    case BridgeStatus::BadClock:return "BAD_CLOCK";
    case BridgeStatus::StalePrices:return "STALE_PRICES";
    case BridgeStatus::StaleWeather:return "STALE_WEATHER";
    case BridgeStatus::Gap:return "PRICE_GAP";
    case BridgeStatus::BadPrice:return "BAD_PRICE";
    case BridgeStatus::BadWeather:return "BAD_WEATHER";
    case BridgeStatus::Invalid:return "INVALID";
    case BridgeStatus::UnalignedStart:return "UNALIGNED_START";
    }
    return "UNKNOWN";
}
const char* horizon_limit_name(HorizonLimit l) {
    switch(l) {
    case HorizonLimit::None:return "NONE";
    case HorizonLimit::MaxSlots:return "MAX_48H";
    case HorizonLimit::PriceEnd:return "PRICES_END";
    case HorizonLimit::PriceGap:return "PRICE_GAP";
    case HorizonLimit::BadPrice:return "BAD_PRICE";
    case HorizonLimit::WeatherEnd:return "WEATHER_END";
    case HorizonLimit::WeatherGap:return "WEATHER_GAP";
    }
    return "UNKNOWN";
}
BridgeResult join_forecast(int64_t now,int64_t price_fetch,int64_t weather_fetch,
    const Spot15* prices,size_t n,const enevina::OutdoorPoint* weather,size_t m,
    const enevina::Tariff& tariff,size_t slots) {
    return join_forecast_two_tariffs(now,price_fetch,weather_fetch,
        prices,n,weather,m,tariff,tariff,slots);
}
} // namespace enevina::cottage
