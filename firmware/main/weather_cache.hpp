#pragma once
// Pure, platform-independent persisted MET Norway forecast contract; no ESP-IDF I/O.
#include "forecast/fmi.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enevina_firmware {
constexpr size_t weather_max_points=64;
constexpr uint32_t weather_cache_version=3;

struct WeatherLocation {
    int32_t latitude_e5=0;
    int32_t longitude_e5=0;
};
inline WeatherLocation normalized_location(double lat,double lon) {
    return {static_cast<int32_t>(std::llround(lat*100000.0)),
            static_cast<int32_t>(std::llround(lon*100000.0))};
}
inline bool operator==(WeatherLocation a,WeatherLocation b) {
    return a.latitude_e5==b.latitude_e5 && a.longitude_e5==b.longitude_e5;
}
struct WeatherCache {
    uint32_t version=weather_cache_version;
    WeatherLocation location{};
    uint32_t count=0;
    int64_t fetched_utc=0;
    // RFC 7234 validators, tied to both provider and configured location.
    int64_t expires_utc=0;
    char last_modified[64]{};
    std::array<enevina::OutdoorPoint,weather_max_points> points{};
    uint32_t checksum=0;
};

// Hash only explicit fields; never hash structure padding. Covers all points,
// including unused slots, to catch corruption of the persisted NVS blob.
inline uint32_t weather_checksum(const WeatherCache& data) {
    uint32_t hash=2166136261u;
    auto mix=[&](uint64_t bits) {
        for(unsigned i=0;i<8;++i) {
            hash^=static_cast<uint8_t>(bits>>(8*i));
            hash*=16777619u;
        }
    };
    mix(data.version);
    mix(static_cast<uint32_t>(data.location.latitude_e5));
    mix(static_cast<uint32_t>(data.location.longitude_e5));
    mix(data.count);
    mix(static_cast<uint64_t>(data.fetched_utc));
    mix(static_cast<uint64_t>(data.expires_utc));
    for(const char c:data.last_modified)
        mix(static_cast<uint8_t>(c));
    for(const auto& p:data.points) {
        mix(static_cast<uint64_t>(p.timestamp_utc));
        uint64_t temperature_bits=0;
        static_assert(sizeof(temperature_bits)==sizeof(p.temperature_c));
        std::memcpy(&temperature_bits,&p.temperature_c,sizeof(temperature_bits));
        mix(temperature_bits);
    }
    return hash;
}
inline bool should_load_weather_cache(bool attempted,int64_t now_utc) {
    return !attempted && now_utc>1700000000;
}
inline bool valid_weather_cache(const WeatherCache& data,
    WeatherLocation configured_location,int64_t now_utc) {
    if(data.version!=weather_cache_version ||
       std::abs(configured_location.latitude_e5)>9000000 ||
       std::abs(configured_location.longitude_e5)>18000000 ||
       !(data.location==configured_location) ||
       data.count<2 || data.count>weather_max_points ||
       now_utc<=1700000000 || data.fetched_utc<=1700000000 ||
       data.fetched_utc>now_utc || now_utc-data.fetched_utc>12*3600 ||
       data.checksum!=weather_checksum(data))
        return false;
    for(size_t i=0;i<data.count;++i) {
        const auto& p=data.points[i];
        if(p.timestamp_utc<=0 || p.timestamp_utc%3600!=0 ||
           !std::isfinite(p.temperature_c) || p.temperature_c < -60 ||
           p.temperature_c>60 ||
           (i>0 && p.timestamp_utc-data.points[i-1].timestamp_utc!=3600))
            return false;
    }
    return data.points[data.count-1].timestamp_utc>now_utc;
}
inline bool same_weather_points(const WeatherCache& a,const WeatherCache& b) {
    if(!(a.location==b.location) || a.count!=b.count) return false;
    for(size_t i=0;i<a.count;++i)
        if(a.points[i].timestamp_utc!=b.points[i].timestamp_utc ||
           a.points[i].temperature_c!=b.points[i].temperature_c) return false;
    return true;
}
} // namespace enevina_firmware
