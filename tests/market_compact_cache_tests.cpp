#include "firmware/main/market_cache_compact.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace enevina_firmware;

int main() {
    PriceCache source{};
    source.count=192;
    source.fetched_utc=1795000000;
    for(size_t i=0;i<source.count;++i) {
        const int64_t start=1795000000+static_cast<int64_t>(i)*900;
        const double price=i%2? -12.345678901234 : 109.123456789012;
        source.prices[i]={start,start+900,price};
    }
    CompactPriceCache packed{};
    assert(pack_price_cache(source,packed));
    assert(packed.version==compact_price_cache_version);
    assert(sizeof(packed)<=2048);
    PriceCache result{};
    assert(unpack_price_cache(packed,result));
    assert(result.count==source.count);
    assert(result.fetched_utc==source.fetched_utc);
    for(size_t i=0;i<source.count;++i) {
        assert(result.prices[i].start_utc==source.prices[i].start_utc);
        assert(result.prices[i].end_utc==source.prices[i].end_utc);
        assert(result.prices[i].eur_per_mwh==source.prices[i].eur_per_mwh);
    }
    source.prices[91].start_utc+=900;
    assert(!pack_price_cache(source,packed));
    source.prices[91].start_utc-=900;
    source.prices[23].end_utc+=900;
    assert(!pack_price_cache(source,packed));
    source.prices[23].end_utc-=900;
    source.prices[17].eur_per_mwh=std::numeric_limits<double>::quiet_NaN();
    assert(!pack_price_cache(source,packed));
    source.prices[17].eur_per_mwh=-12.345678901234;
    assert(pack_price_cache(source,packed));
    packed.version=1;
    assert(!unpack_price_cache(packed,result));
    packed.version=compact_price_cache_version;
    packed.count=201;
    assert(!unpack_price_cache(packed,result));
    packed.count=192;
    packed.prices_eur_per_mwh[9]=std::numeric_limits<double>::infinity();
    assert(!unpack_price_cache(packed,result));
    // A regional blob carries its own identity. Neither a different
    // area's current settings nor an external stamp can relabel it.
    RegionalCompactPriceCache regional{};
    assert(pack_regional_price_cache(source,"SE3","SEK",regional));
    assert(unpack_regional_price_cache(regional,"SE3","SEK",result));
    assert(result.prices[9].eur_per_mwh==source.prices[9].eur_per_mwh);
    const auto previous=result.prices[9].eur_per_mwh;
    assert(!unpack_regional_price_cache(regional,"SE4","SEK",result));
    assert(!unpack_regional_price_cache(regional,"SE3","EUR",result));
    assert(!unpack_regional_price_cache(regional,"FI","EUR",result));
    assert(result.prices[9].eur_per_mwh==previous);
    regional.version=2;
    assert(!unpack_regional_price_cache(regional,"SE3","SEK",result));
    regional.version=3;
    std::memset(regional.area,'X',sizeof(regional.area));
    assert(!unpack_regional_price_cache(regional,"SE3","SEK",result));
    assert(!pack_regional_price_cache(source,"TOO_LONG_REGION","SEK",regional));
    assert(!pack_regional_price_cache(source,"FI","EU",regional));
}
