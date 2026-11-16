#include "firmware/main/weather_cache.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace enevina_firmware;
static void check(bool ok,const char* reason) {
    if(!ok) {std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}
}
int main() {
    constexpr int64_t now=1800000000;
    const auto loc=normalized_location(61.47495,23.98765);
    const auto different=normalized_location(60.19210,24.94580);
    check(!(loc==different),"different coordinates need different location IDs");
    WeatherCache cache{};
    cache.location=loc;
    cache.fetched_utc=now-1800;
    cache.count=3;
    const int64_t hour=(now/3600)*3600;
    for(size_t i=0;i<cache.count;++i)
        cache.points[i]={hour+3600*static_cast<int64_t>(i+1),-12.5+double(i)};
    cache.checksum=weather_checksum(cache);
    check(valid_weather_cache(cache,loc,now),"same location cache accepted");
    check(!valid_weather_cache(cache,different,now),
          "different configured location rejects cached weather after failed fetch");
    check(!valid_weather_cache(cache,{},now),"missing configured location rejects weather");
    check(!valid_weather_cache(cache,loc,now+13*3600),"stale forecast rejected");
    auto changed=cache;
    changed.version=1;
    check(!valid_weather_cache(changed,loc,now),"legacy cache rejected");
    changed=cache;
    changed.points[1].temperature_c=27;
    check(!valid_weather_cache(changed,loc,now),"corrupt temperature checksum rejected");
    changed=cache;
    changed.checksum^=1;
    check(!valid_weather_cache(changed,loc,now),"corrupt checksum rejected");
    changed=cache;
    changed.points[1].timestamp_utc+=3600;
    changed.checksum=weather_checksum(changed);
    check(!valid_weather_cache(changed,loc,now),"discontinuous hourly timestamps rejected");
    changed=cache;
    changed.fetched_utc=now+1;
    changed.checksum=weather_checksum(changed);
    check(!valid_weather_cache(changed,loc,now),"future fetch instant rejected");
    changed=cache;
    changed.count=weather_max_points+1;
    changed.checksum=weather_checksum(changed);
    check(!valid_weather_cache(changed,loc,now),"point bounds enforced");
    WeatherCache reloaded{};
    const auto* bytes=reinterpret_cast<const unsigned char*>(&cache);
    std::memcpy(&reloaded,bytes,sizeof(reloaded));
    check(valid_weather_cache(reloaded,loc,now),"valid cache after restart");
    check(!valid_weather_cache(reloaded,different,now),
          "reboot after a coordinate change cannot reuse old weather");
    check(!same_weather_points(cache,changed),"count changes detected for persistence");
    std::puts("PASS weather location, age, integrity, continuity, migration and reboot");
}
