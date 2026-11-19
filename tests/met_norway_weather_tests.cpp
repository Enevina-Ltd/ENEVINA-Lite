#include "forecast/met_norway.hpp"
#include "forecast/met_http_cache.hpp"
#include "firmware/main/met_gzip.hpp"
#include "cottage/site_settings.hpp"
#include "firmware/main/weather_cache.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
using namespace enevina;
static std::string stamp(int hour,int day=18) {
    char b[36];std::snprintf(b,sizeof(b),"2026-11-%02dT%02d:00:00Z",
                          day,hour);return b;
}
static std::string sample(size_t n,bool add_six_hour=false) {
    std::string value=R"({"type":"Feature","properties":{"meta":{"updated_at":"2026-11-18T20:28:58Z"},"timeseries":[)";
    for(size_t i=0;i<n;++i){
        if(i)value+=",";
        const int hour=21+int(i);
        const int day=18+hour/24, hr=hour%24;
        value+=R"({"time":")"+stamp(hr,day)+
            R"(","data":{"instant":{"details":{"air_temperature":)" +
            std::to_string(-7.5+double(i)/4.0) +
            R"(,"wind_speed":3.1}},"next_1_hours":{"details":{"precipitation_amount":0.1}}}})";
    }
    if(add_six_hour) {
        value+=R"(,{"time":")"+stamp(2,21)+
             R"(","data":{"instant":{"details":{"air_temperature":3.0}}}})";
    }
    value+="]},\"geometry\":{\"type\":\"Point\",\"coordinates\":[24.1,61.4]}}";
    return value;
}
int main(){
    OutdoorPoint out[64]{};
    size_t count=0;
    auto s=sample(40);
    assert(parse_met_norway_temperature(s.c_str(),out,64,count));
    assert(count==40);
    assert(out[0].temperature_c==-7.5);
    assert(std::fabs(out[39].temperature_c-(-7.5+39.0/4))<1e-8);
    for(size_t i=1;i<count;++i)
        assert(out[i].timestamp_utc-out[i-1].timestamp_utc==3600);
    s=sample(48,true);
    assert(parse_met_norway_temperature(s.c_str(),out,64,count));
    assert(count==48);
    s=sample(64);
    assert(parse_met_norway_temperature(s.c_str(),out,64,count));
    assert(count==64);
    s=sample(2);
    assert(parse_met_norway_temperature(s.c_str(),out,64,count));
    assert(count==2);
    s=sample(2);
    s.replace(s.find("air_temperature"),15,"wrong_property");
    assert(!parse_met_norway_temperature(s.c_str(),out,64,count));
    s=sample(2);
    s.erase(s.size()-4);
    assert(!parse_met_norway_temperature(s.c_str(),out,64,count));
    s=sample(2);
    s.replace(s.find("00:00Z"),6,"15:00Z");
    assert(!parse_met_norway_temperature(s.c_str(),out,64,count));
    assert(!parse_met_norway_temperature(nullptr,out,64,count));
    assert(!parse_met_norway_temperature(s.c_str(),out,1,count));
    assert(met::expires_utc("Wed, 18 Nov 2026 21:39:50 GMT")==1795037990);
    assert(met::expires_utc("Wed, 18 Nov 2026 99:39:50 GMT")==0);
    assert(met::expires_utc("Wed, 31 Feb 2026 21:39:50 GMT")==0);
    assert(met::min_safe_retry(1800000000,0)==1800003600);
    assert(met::min_safe_retry(1800000000,1800000300)==1800000300);
    const uint8_t header[]={0x1f,0x8b,0x08,0,0,0,0,0,0,3,1,2,3,4,5,6,7,8,9};
    size_t from=0,to=0;uint32_t crc=0,isize=0;
    assert(enevina_firmware::met_gzip_payload(header,sizeof(header),from,to,crc,isize));
    assert(from==10 && to==11);
    assert(!enevina_firmware::met_gzip_payload(nullptr,0,from,to,crc,isize));
    assert(enevina::cottage::detail::valid_latlon("-33.8688,151.2093"));
    assert(enevina::cottage::detail::valid_latlon("0,0"));
    assert(enevina::cottage::detail::valid_latlon("61.4540,24.0810"));
    assert(!enevina::cottage::detail::valid_latlon("100,0"));
    assert(!enevina::cottage::detail::valid_latlon("61.45,200"));
    assert(!enevina::cottage::detail::valid_latlon("61.45,foo"));
    enevina_firmware::WeatherCache cache{};
    cache.location=enevina_firmware::normalized_location(0,0);
    cache.count=2;
    cache.points[0]={1795035600,-7};
    cache.points[1]={1795039200,-5};
    cache.fetched_utc=1795035600;
    cache.expires_utc=1795037990;
    std::strcpy(cache.last_modified,"Wed, 18 Nov 2026 20:28:58 GMT");
    cache.checksum=enevina_firmware::weather_checksum(cache);
    assert(enevina_firmware::valid_weather_cache(
        cache,enevina_firmware::normalized_location(0,0),1795037000));
    cache.last_modified[0]='?';
    assert(!enevina_firmware::valid_weather_cache(
        cache,enevina_firmware::normalized_location(0,0),1795037000));
}
