#pragma once
// Reclaim ONLY disposable remote-data caches when durable user/site settings
// cannot fit in a small NVS partition. Never erase Wi-Fi, OTA, sensor history,
// device identity or control authorization.
namespace enevina::cottage {
struct NvsDisposableCache {
    const char* nspace;
    const char* key;
};
constexpr NvsDisposableCache kDisposableSiteRecovery[]={
    {"enevina_weather","weather_v2"},
    {"enevina_market","price_v2"},
};
template<class Store,class Erase>
bool persist_with_optional_cache_reclaim(Store store,Erase erase) {
    if(store()) return true;
    for(const auto& cache:kDisposableSiteRecovery) {
        if(erase(cache.nspace,cache.key) && store())return true;
    }
    return false;
}
} // namespace enevina::cottage
