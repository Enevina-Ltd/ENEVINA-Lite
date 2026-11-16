#include "firmware/main/market_cache.hpp"
#include "forecast/sources.hpp"
#include <cassert>
#include <cstdio>
using namespace enevina;
using namespace enevina_firmware;

static void set(PriceCache& c,size_t n,int64_t first,double price) {
    c.count=static_cast<uint32_t>(n);
    for(size_t i=0;i<n;++i) {
        const auto start=first+static_cast<int64_t>(i*900);
        c.prices[i]={start,start+900,price};
    }
}
static bool valid(const PriceCache& c) {
    PriceSlot normalized[price_cache_max_slots]{};
    const Tariff tariff{};
    return normalize_nordpool_fi(c.prices.data(),c.count,tariff,
                               normalized,price_cache_max_slots);
}
int main() {
    constexpr int64_t start=1800000000;
    static_assert(start%900==0,"epoch aligned for 15-minute slots");
    PriceCache old{};
    set(old,8,start,150);
    assert(valid(old));
    // Two validated calendar days were cached, but only today refetched.
    PriceCache fresh{};
    set(fresh,4,start,200);
    assert(valid(fresh));
    assert(preserve_future_price_tail(old,fresh)==4);
    assert(fresh.count==8 && valid(fresh));
    for(size_t i=0;i<4;++i) assert(fresh.prices[i].eur_per_mwh==200);
    for(size_t i=4;i<8;++i) assert(fresh.prices[i].eur_per_mwh==150);
    // Reboot-equivalent data must retain midnight's first price.
    assert(fresh.prices[4].start_utc==start+4*900);
    PriceCache again{};
    set(again,4,start,200);
    assert(preserve_future_price_tail(fresh,again)==4 &&
           again.count==8 && valid(again));
    // Fully refreshed values override all previously saved values.
    PriceCache fully{};
    set(fully,8,start,300);
    assert(preserve_future_price_tail(fresh,fully)==0);
    assert(fully.count==8 && valid(fully));
    for(size_t i=0;i<8;++i) assert(fully.prices[i].eur_per_mwh==300);
    // Gaps must never be bridged with old prices.
    PriceCache gap{};
    set(gap,3,start+5*900,150);
    PriceCache today{};
    set(today,4,start,200);
    assert(preserve_future_price_tail(gap,today)==0);
    assert(today.count==4 && valid(today));
    // Old expired slots must not be reintroduced.
    PriceCache expired{};
    set(expired,4,start-4*900,150);
    PriceCache new_day{};
    set(new_day,4,start,200);
    assert(preserve_future_price_tail(expired,new_day)==0);
    assert(new_day.count==4 && valid(new_day));
    PriceCache empty{};
    assert(preserve_future_price_tail(old,empty)==0);
    // Fixed capacity survives even a maximal DST-affected window.
    PriceCache full{};
    set(full,price_cache_max_slots,start,500);
    assert(preserve_future_price_tail(old,full)==0);
    assert(full.count==price_cache_max_slots && valid(full));
    std::puts("PASS price tail preserves previously published next-day prices");
}
