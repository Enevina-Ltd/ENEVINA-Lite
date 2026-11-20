#pragma once
// Data freshness and offline behaviour, explained from the same facts the
// controller uses. Pure C++ (host-tested); the firmware gathers the inputs and
// streams the JSON.
//
// Rules mirror the live controller (cottage/price_bootstrap.hpp + main.cpp):
//  * price decisions need a trusted clock, prices fetched within 36 h and at
//    least 8 contiguous slots (2 h) ahead; otherwise the room is a plain
//    thermostat on target +-0.5 C (never a stale-price decision);
//  * without a fresh indoor measurement no heating proposal is made at all;
//  * old forecasts and prices are never presented as current: every source
//    states its own last good time, expiry and reason.
// Going offline therefore changes nothing until cached data expires; the report
// says which source expires when and what the controller does afterwards.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>

namespace enevina::cottage {
enum class SourceState : uint8_t {
    Ok=0,            // usable now with margin
    Short=1,         // usable but ends within 2 h (controller will fall back soon)
    Stale=2,         // exists but too old / not heard for too long to use
    Expired=3,       // was valid, validity ended
    Missing=4,       // never obtained since boot
    NotConfigured=5, // the owner has not set this up
    Error=6          // last attempt failed with a known reason
};
inline const char* source_state_name(SourceState s) {
    switch(s) {
    case SourceState::Ok:return "ok";
    case SourceState::Short:return "short";
    case SourceState::Stale:return "stale";
    case SourceState::Expired:return "expired";
    case SourceState::Missing:return "missing";
    case SourceState::NotConfigured:return "not_configured";
    case SourceState::Error:return "error";
    }
    return "missing";
}
enum class ControllerFallback : uint8_t {
    PriceControl=0,   // fresh prices + fresh room sensor
    Thermostat=1,     // room sensor fresh, prices unusable
    Blocked=2,        // no fresh room measurement: no proposal
    NoHeatingRequested=3 // Cold mode or Drying humidity gate prohibits heat
};
inline const char* fallback_name(ControllerFallback f) {
    switch(f) {
    case ControllerFallback::PriceControl:return "price_control";
    case ControllerFallback::Thermostat:return "thermostat";
    case ControllerFallback::Blocked:return "blocked";
    case ControllerFallback::NoHeatingRequested:return "no_heating_requested";
    }
    return "blocked";
}
constexpr int64_t kPriceMaxAgeS=36*3600;          // same limit the controller applies
constexpr int64_t kPriceMinAheadS=8*900;          // comparison window minimum

struct FreshnessInputs {
    bool clock_trusted=false;
    int64_t now=0;
    bool wifi_ok=false;
    // prices
    bool price_zone_configured=false;
    bool price_current_valid=false;
    bool price_decision_valid=false; // ACTUAL bootstrap decision validated tariff and 8-slot totals
    bool no_heating_requested=false; // ACTUAL bootstrap Off/NoHeatingRequested action
    int64_t price_last_fetch=0,price_valid_until=0;
    uint8_t price_error=0;
    // weather forecast
    bool weather_configured=false,weather_available=false;
    int64_t weather_fetched=0,weather_valid_until=0;
    // room sensor / pump data
    bool sensor_bound=false,sensor_fresh=false;
    int64_t sensor_age_s=-1;
    bool pump_configured=false,pump_fresh=false;
    int64_t pump_age_s=-1;
    bool control_armed=false;
};
struct SourceStatus {
    SourceState state=SourceState::Missing;
    int64_t last_ok_utc=0;        // last successful fetch / reading (0 unknown)
    int64_t valid_until_utc=0;    // when cached data stops being usable (0 n/a)
    int64_t age_s=-1;             // for live sources
    uint8_t error=0;
};
struct Freshness {
    bool clock_ok=false,wifi_ok=false;
    bool offline=false;           // the network is down (the device keeps working locally)
    SourceStatus prices,weather,sensor,pump;
    ControllerFallback fallback=ControllerFallback::Blocked;
    int64_t last_contact_utc=0;   // newest successful external fetch (prices/weather)
    int64_t next_expiry_utc=0;    // earliest moment cached data stops being usable, 0 = none
};

// The displayed pump age must refer to the source that actually proved fresh.
// Direct CN105 and optional SSE have different capture timestamps.
inline int64_t verified_pump_age(int64_t now,bool direct_valid,int64_t direct_utc,
                                 bool sse_valid,int64_t sse_utc) {
    const int64_t captured=direct_valid?direct_utc:sse_valid?sse_utc:0;
    return captured>0 && now>=captured?now-captured:-1;
}
inline Freshness assess_freshness(const FreshnessInputs& in) {
    Freshness f;
    f.clock_ok=in.clock_trusted;f.wifi_ok=in.wifi_ok;f.offline=!in.wifi_ok;
    const int64_t now=in.now;
    // ---- prices ----
    SourceStatus& p=f.prices;
    p.last_ok_utc=in.price_last_fetch;p.valid_until_utc=in.price_valid_until;p.error=in.price_error;
    if(!in.price_zone_configured) p.state=SourceState::NotConfigured;
    else if(!in.clock_trusted) p.state=in.price_last_fetch>0?SourceState::Stale:SourceState::Missing;
    else if(in.price_current_valid&&in.price_last_fetch>0&&in.price_last_fetch<=now&&
            now-in.price_last_fetch<=kPriceMaxAgeS) {
        // The live controller uses the current 15-minute slot START as its window anchor.
        p.state=in.price_valid_until>=((now/900)*900+kPriceMinAheadS)?SourceState::Ok:SourceState::Short;
    } else if(in.price_last_fetch>0) {
        // had prices once: too old (36 h rule) or their validity ended
        const bool too_old=now-in.price_last_fetch>kPriceMaxAgeS;
        p.state=in.price_error?SourceState::Error:too_old?SourceState::Stale:SourceState::Expired;
    } else p.state=in.price_error?SourceState::Error:SourceState::Missing;
    // ---- weather ----
    SourceStatus& w=f.weather;
    w.last_ok_utc=in.weather_fetched;w.valid_until_utc=in.weather_valid_until;
    if(!in.weather_configured) w.state=SourceState::NotConfigured;
    else if(!in.clock_trusted) w.state=in.weather_fetched>0?SourceState::Stale:SourceState::Missing;
    else if(in.weather_available&&in.weather_valid_until>now) w.state=SourceState::Ok;
    else w.state=in.weather_fetched>0?SourceState::Expired:SourceState::Missing;
    // ---- live sources ----
    SourceStatus& s=f.sensor;
    s.age_s=in.sensor_age_s;
    s.state=!in.sensor_bound?SourceState::NotConfigured:in.sensor_fresh?SourceState::Ok:SourceState::Stale;
    SourceStatus& u=f.pump;
    u.age_s=in.pump_age_s;
    u.state=!in.pump_configured?SourceState::NotConfigured:in.pump_fresh?SourceState::Ok:SourceState::Stale;
    // ---- what the controller does (same conditions as main.cpp) ----
    const bool prices_usable=p.state==SourceState::Ok && in.price_decision_valid;
    if(in.no_heating_requested) f.fallback=ControllerFallback::NoHeatingRequested;
    else if(s.state!=SourceState::Ok) f.fallback=ControllerFallback::Blocked;
    else if(prices_usable) f.fallback=ControllerFallback::PriceControl;
    else f.fallback=ControllerFallback::Thermostat;
    f.last_contact_utc=std::max(in.price_last_fetch,in.weather_fetched);
    // Earliest expiry among sources that are currently usable.
    int64_t exp=0;
    auto consider=[&](bool usable,int64_t until) {
        if(usable&&until>now&&(exp==0||until<exp)) exp=until;
    };
    consider(p.state==SourceState::Ok||p.state==SourceState::Short,
             std::min(in.price_valid_until,in.price_last_fetch+kPriceMaxAgeS));
    consider(w.state==SourceState::Ok,in.weather_valid_until);
    f.next_expiry_utc=exp;
    return f;
}

template<class Emit>
inline void freshness_json(const Freshness& f,bool control_armed,
                           const char* control_gate,Emit emit) {
    char b[280];
    // Gate is the live controller's own ControlBlock, not inferred from
    // optional SSE/UART data freshness. No commands are claimed without it.
    std::snprintf(b,sizeof(b),
        "{\"clock_ok\":%s,\"wifi_ok\":%s,\"offline\":%s,\"fallback\":\"%s\",\"control_armed\":%s,"
        "\"control_gate\":\"%s\",\"last_contact_utc\":%lld,\"next_expiry_utc\":%lld,\"sources\":[",
        f.clock_ok?"true":"false",f.wifi_ok?"true":"false",f.offline?"true":"false",
        fallback_name(f.fallback),control_armed?"true":"false",
        control_gate?control_gate:"UNKNOWN",
        static_cast<long long>(f.last_contact_utc),static_cast<long long>(f.next_expiry_utc));
    emit(b);
    const struct {const char* id;const SourceStatus* s;} rows[]={
        {"prices",&f.prices},{"weather",&f.weather},{"sensor",&f.sensor},{"pump",&f.pump}};
    for(size_t i=0;i<4;++i) {
        const SourceStatus& s=*rows[i].s;
        std::snprintf(b,sizeof(b),
            "%s{\"id\":\"%s\",\"state\":\"%s\",\"last_ok_utc\":%lld,\"valid_until_utc\":%lld,\"age_s\":%lld,\"error\":%u}",
            i?",":"",rows[i].id,source_state_name(s.state),static_cast<long long>(s.last_ok_utc),
            static_cast<long long>(s.valid_until_utc),static_cast<long long>(s.age_s),s.error);
        emit(b);
    }
    emit("]}");
}
} // namespace enevina::cottage
