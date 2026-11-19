#pragma once
// Compact, lossless versioned NVS storage for Finnish 15-minute market prices.
// The firmware's RAM PriceCache remains unchanged. This saves ~2/3 of NVS space
// while preserving every double exactly, including negative spot prices.
#include "market_cache.hpp"
#include <cmath>
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
    CompactPriceCache candidate{};
    candidate.count=static_cast<uint32_t>(source.count);
    candidate.fetched_utc=source.fetched_utc;
    candidate.first_start_utc=first;
    for(size_t i=0;i<source.count;++i) {
        const auto& price=source.prices[i];
        const int64_t start=first+
            static_cast<int64_t>(i)*compact_price_step_seconds;
        if(price.start_utc!=start ||
           price.end_utc!=start+compact_price_step_seconds ||
           !std::isfinite(price.eur_per_mwh))
            return false;
        candidate.prices_eur_per_mwh[i]=price.eur_per_mwh;
    }
    out=candidate;
    return true;
}

inline bool unpack_price_cache(const CompactPriceCache& stored,PriceCache& out) {
    if(stored.version!=compact_price_cache_version ||
       !price_cache_time_range_ok(stored.first_start_utc,stored.count))
        return false;
    PriceCache candidate{};
    candidate.count=stored.count;
    candidate.fetched_utc=stored.fetched_utc;
    for(size_t i=0;i<stored.count;++i) {
        const double value=stored.prices_eur_per_mwh[i];
        if(!std::isfinite(value))return false;
        const int64_t start=stored.first_start_utc+
            static_cast<int64_t>(i)*compact_price_step_seconds;
        candidate.prices[i]={start,start+compact_price_step_seconds,value};
    }
    out=candidate;
    return true;
}
} // namespace enevina_firmware
