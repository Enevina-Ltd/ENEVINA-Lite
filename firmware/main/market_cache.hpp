#pragma once
// Pure no-allocation Nord Pool cache tail preservation. ESP-IDF I/O stays in
// market.cpp; input caches are validated before the merge and after it.
#include "forecast/sources.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace enevina_firmware {
constexpr size_t price_cache_max_slots=200; // Two Finnish civil days, DST inclusive.
constexpr uint32_t price_cache_version=1;
struct PriceCache {
    uint32_t version=price_cache_version;
    uint32_t count=0;
    int64_t fetched_utc=0;
    std::array<enevina::MarketPrice,price_cache_max_slots> prices{};
};

// The latest fetched data wins for overlapping slots. On partial refresh,
// retain only a contiguous still-future suffix from an earlier valid cache;
// never prepend stale data, splice across gaps, or exceed fixed capacity.
inline size_t preserve_future_price_tail(const PriceCache& previous,
                                         PriceCache& refreshed) {
    if(refreshed.count==0 || refreshed.count>price_cache_max_slots ||
       previous.count>price_cache_max_slots)
        return 0;
    size_t appended=0;
    int64_t end=refreshed.prices[refreshed.count-1].end_utc;
    for(size_t i=0;i<previous.count && refreshed.count<price_cache_max_slots;++i) {
        const auto& slot=previous.prices[i];
        if(slot.end_utc<=end) continue; // Fresh interval overrides stale one.
        if(slot.start_utc!=end) break;   // No blending across missing intervals.
        refreshed.prices[refreshed.count++]=slot;
        end=slot.end_utc;
        ++appended;
    }
    return appended;
}
} // namespace enevina_firmware
