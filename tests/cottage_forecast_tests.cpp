#include "cottage/forecast_bridge.hpp"
#include "cottage/ruuvi.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
using namespace enevina;
using namespace enevina::cottage;
int main(){
    constexpr int64_t t0=1792875600; // Finland DST, UTC time
    std::vector<Spot15> prices;
    for(size_t i=0;i<192;i++)
        prices.push_back({t0+static_cast<int64_t>(900*i),
            i==0?-0.05:0.05});
    std::vector<OutdoorPoint> weather;
    for(size_t i=0;i<=50;i++)
        weather.push_back({t0+static_cast<int64_t>(3600*i),-5.0});
    Tariff t{};
    t.vat_rate=0.255;
    t.margin_eur_per_kwh=0.005;
    t.transfer_eur_per_kwh=0.04;
    t.electricity_tax_eur_per_kwh=0.02;
    const auto good=join_forecast(t0,t0-30,t0-30,prices.data(),
        prices.size(),weather.data(),weather.size(),t,192);
    assert(good.status==BridgeStatus::Ready && good.count==192);
    assert(good.first_utc==t0);
    assert(good.periods[0].total_price_eur_per_kwh<0.03);
    assert(good.periods[1].total_price_eur_per_kwh>0.14);
    assert(good.periods[0].outdoor_c==-5);
    assert(join_forecast(1,t0-30,t0-30,prices.data(),
        prices.size(),weather.data(),weather.size(),t,96).status==BridgeStatus::BadClock);
    assert(join_forecast(t0,t0-37*3600,t0-30,prices.data(),
        prices.size(),weather.data(),weather.size(),t,96).status==BridgeStatus::StalePrices);
    assert(join_forecast(t0,t0-30,t0-7*3600,prices.data(),
        prices.size(),weather.data(),weather.size(),t,96).status==BridgeStatus::StaleWeather);
    prices[3].utc+=900;
    assert(join_forecast(t0,t0-30,t0-30,prices.data(),
        prices.size(),weather.data(),weather.size(),t,96).status==BridgeStatus::Gap);
    prices[3].utc-=900;
    prices[3].eur_per_kwh=std::numeric_limits<double>::quiet_NaN();
    assert(join_forecast(t0,t0-30,t0-30,prices.data(),
        prices.size(),weather.data(),weather.size(),t,96).status==BridgeStatus::BadPrice);
    prices[3].eur_per_kwh=0.05;
    const uint8_t sample[]={0x99,0x04,0x05,0x04,0xD2};
    double temp=0;
    assert(decode_ruuvi_rawv5_temperature(sample,sizeof sample,temp));
    assert(std::abs(temp-6.17)<1e-8);
    const uint8_t invalid[]={0x99,0x04,0x05,0x80,0x00};
    assert(!decode_ruuvi_rawv5_temperature(invalid,sizeof invalid,temp));
    puts("PASS forecast bridge and BLE rawv5 tests");
}
