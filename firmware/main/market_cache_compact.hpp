#pragma once
// Compact, lossless versioned NVS storage for Finnish 15-minute market prices.
// The firmware's RAM PriceCache remains unchanged. This saves ~2/3 of NVS space
// while preserving every double exactly, including negative spot prices.
#include "market_cache.hpp"
#include <cmath>
#include <cstring>
#include <string_view>
#include <limits>

namespace enevina_firmware {

constexpr uint32_t compact_price_cache_version=2;
constexpr int64_t compact_price_step_seconds=900;
struct CompactPriceCache {
    uint32_t version=compact_price_cache_version;
    uint32_t count=0;
    int64_t fetched_utc=0;
    int64_t first_start_utc=0;
    std::array<double,price_cache_max_slots> prices_eur_per_mwh{};
};
static_assert(sizeof(CompactPriceCache)<=2048,
              "Price cache must fit in a small NVS blob");

inline bool price_cache_time_range_ok(int64_t first,size_t count) {
    return count>0 && count<=price_cache_max_slots && first>=0 &&
        first<=std::numeric_limits<int64_t>::max()-
            static_cast<int64_t>(count)*compact_price_step_seconds;
}

inline bool pack_price_cache(const PriceCache& source,CompactPriceCache& out) {
    if(source.version!=price_cache_version ||
       !price_cache_time_range_ok(source.prices[0].start_utc,source.count))
        return false;
    const int64_t first=source.prices[0].start_utc;
    // Validate before writing the output, without a 1.6 KiB stack copy.
    for(size_t i=0;i<source.count;++i) {
        const auto& price=source.prices[i];
        const int64_t start=first+static_cast<int64_t>(i)*compact_price_step_seconds;
        if(price.start_utc!=start || price.end_utc!=start+compact_price_step_seconds ||
           !std::isfinite(price.eur_per_mwh))return false;
    }
    out.version=compact_price_cache_version;
    out.count=static_cast<uint32_t>(source.count);
    out.fetched_utc=source.fetched_utc;
    out.first_start_utc=first;
    out.prices_eur_per_mwh.fill(0);
    for(size_t i=0;i<source.count;++i)
        out.prices_eur_per_mwh[i]=source.prices[i].eur_per_mwh;
    return true;
}

inline bool unpack_price_cache(const CompactPriceCache& stored,PriceCache& out) {
    if(stored.version!=compact_price_cache_version ||
       !price_cache_time_range_ok(stored.first_start_utc,stored.count))
        return false;
    for(size_t i=0;i<stored.count;++i)
        if(!std::isfinite(stored.prices_eur_per_mwh[i]))return false;
    // Avoid the old 4.8 KiB temporary on the market worker stack.
    out.version=price_cache_version;
    out.count=stored.count;out.fetched_utc=stored.fetched_utc;
    out.prices.fill({});
    for(size_t i=0;i<stored.count;++i) {
        const int64_t start=stored.first_start_utc+static_cast<int64_t>(i)*compact_price_step_seconds;
        out.prices[i]={start,start+compact_price_step_seconds,stored.prices_eur_per_mwh[i]};
    }
    return true;
}
// Regional identity is part of ONE persisted NVS blob. The historical
// Finland-only key retains its exact format for firmware rollback.
struct RegionalCompactPriceCache {
    uint32_t magic=0x33435052u; // RPC3
    uint32_t version=3;
    char area[8]{},currency[4]{};
    CompactPriceCache data{};
};
static_assert(sizeof(RegionalCompactPriceCache)<=2048);
inline bool pack_regional_price_cache(const PriceCache& source,const char* area,
        const char* currency,RegionalCompactPriceCache& out) {
    if(!area||!currency||std::strlen(area)<2||std::strlen(area)>=sizeof(out.area)||
       std::strlen(currency)!=3||!pack_price_cache(source,out.data))return false;
    out.magic=0x33435052u;out.version=3;
    std::memset(out.area,0,sizeof(out.area));std::memset(out.currency,0,sizeof(out.currency));
    std::strcpy(out.area,area);std::strcpy(out.currency,currency);
    return true;
}
inline bool unpack_regional_price_cache(const RegionalCompactPriceCache& stored,
        const char* area,const char* currency,PriceCache& out) {
    if(!area||!currency||stored.magic!=0x33435052u||stored.version!=3||
       !std::memchr(stored.area,0,sizeof(stored.area))||
       !std::memchr(stored.currency,0,sizeof(stored.currency))||
       std::strcmp(stored.area,area)!=0||std::strcmp(stored.currency,currency)!=0)
        return false;
    return unpack_price_cache(stored.data,out);
}
} // namespace enevina_firmware
