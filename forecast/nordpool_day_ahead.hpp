#pragma once
// Area- and currency-parametrized Nord Pool Data Portal day-ahead client
// logic: URL construction and strict response parsing. ESP-IDF HTTP I/O stays
// in firmware/main/market.cpp, so everything here is host-tested against real
// recorded responses (tests/data/nordpool).
//
// Contract of a successful parse:
//  * every slot is exactly 900 s, strictly consecutive, UTC-aligned;
//  * the response contains exactly the requested delivery area;
//  * the response currency equals the requested currency;
//  * 92..100 slots (23 h..25 h market day, DST-aware);
//  * values are finite, in a plausible band, and never invented.
#include "forecast/market_zones.hpp"
#include "forecast/met_norway.hpp" // shared strict no-allocation JSON scanner
#include "forecast/sources.hpp"
#include <cmath>
#include <cstdio>
#include <string_view>

namespace enevina {
enum class DayAheadError : uint8_t {
    Ok, BadRequest, BadJson, WrongArea, WrongCurrency, BadSlots, BadValue, Capacity
};
inline const char* day_ahead_error_name(DayAheadError e) {
    switch(e) {
    case DayAheadError::Ok:return "OK";
    case DayAheadError::BadRequest:return "BAD_REQUEST";
    case DayAheadError::BadJson:return "BAD_JSON";
    case DayAheadError::WrongArea:return "WRONG_AREA";
    case DayAheadError::WrongCurrency:return "WRONG_CURRENCY";
    case DayAheadError::BadSlots:return "BAD_SLOTS";
    case DayAheadError::BadValue:return "BAD_VALUE";
    case DayAheadError::Capacity:return "CAPACITY";
    }
    return "UNKNOWN";
}

namespace np_detail {
inline bool upper_alnum_dash(const char* s,size_t min_len,size_t max_len) {
    if(!s) return false;
    const size_t n=std::strlen(s);
    if(n<min_len||n>max_len) return false;
    for(size_t i=0;i<n;++i)
        if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')||s[i]=='-')) return false;
    return true;
}
inline bool valid_date(const char* d) {
    if(!d||std::strlen(d)!=10||d[4]!='-'||d[7]!='-') return false;
    for(int i:{0,1,2,3,5,6,8,9}) if(d[i]<'0'||d[i]>'9') return false;
    return true;
}
inline bool plain_number(std::string_view v,double& out) {
    if(v.empty()||v.size()>=32) return false;
    met_detail::Json c{v.data(),v.data()+v.size()};
    if(!c.number()) return false;
    c.ws();
    if(c.cur!=c.end) return false;
    char b[32]{};
    std::memcpy(b,v.data(),v.size());
    char* stop=nullptr;
    const double x=std::strtod(b,&stop);
    if(stop==b||*stop||!std::isfinite(x)) return false;
    out=x;
    return true;
}
} // namespace np_detail

// Only whitelisted characters reach the URL, so a corrupted setting can never
// inject query parameters.
inline bool build_day_ahead_url(const char* area,const char* currency,
                                const char* date,char* out,size_t cap) {
    if(!out||!np_detail::upper_alnum_dash(area,2,6)||
       !np_detail::upper_alnum_dash(currency,3,3)||!np_detail::valid_date(date))
        return false;
    const int n=std::snprintf(out,cap,
        "https://dataportal-api.nordpoolgroup.com/api/DayAheadPrices?"
        "currency=%s&market=DayAhead&deliveryArea=%s&date=%s",currency,area,date);
    return n>0 && static_cast<size_t>(n)<cap;
}

// Plausible spot band in the REQUESTED currency per MWh. The widest real
// ranges (PLN/HUF-class currencies) fit; it exists to reject unit mix-ups, not
// to police the market. Negative prices are legitimate.
constexpr double kDayAheadMinPerMwh=-5000.0;
constexpr double kDayAheadMaxPerMwh=100000.0;

inline DayAheadError parse_day_ahead(const char* json,size_t length,
    const char* area,const char* currency,MarketPrice* out,size_t capacity,size_t& count) {
    count=0;
    if(!json||!out||!np_detail::upper_alnum_dash(area,2,6)||
       !np_detail::upper_alnum_dash(currency,3,3)) return DayAheadError::BadRequest;
    if(length<40||length>64*1024) return DayAheadError::BadJson;
    const std::string_view doc(json,length);
    met_detail::Json validator{doc.data(),doc.data()+doc.size()};
    if(!validator.value()) return DayAheadError::BadJson;
    validator.ws();
    if(validator.cur!=validator.end) return DayAheadError::BadJson;

    std::string_view currency_v,entries_v;
    char text[16]{};
    if(!met_detail::member(doc,"currency",currency_v)||
       !met_detail::decode_string(currency_v,text,sizeof(text))||
       !met_detail::member(doc,"multiAreaEntries",entries_v)) return DayAheadError::BadJson;
    if(std::strcmp(text,currency)!=0) return DayAheadError::WrongCurrency;

    met_detail::Json array{entries_v.data(),entries_v.data()+entries_v.size()};
    array.ws();
    if(array.cur>=array.end||*array.cur++!='[') return DayAheadError::BadJson;
    size_t n=0;
    for(;;) {
        array.ws();
        if(array.cur>=array.end) return DayAheadError::BadJson;
        if(*array.cur==']') {++array.cur;break;}
        std::string_view entry,start_v,end_v,per_area,price_v;
        if(!array.span(entry)) return DayAheadError::BadJson;
        if(n>=capacity||n>=100) return DayAheadError::Capacity;
        char start_t[40]{},end_t[40]{};
        int64_t start=0,end=0;
        if(!met_detail::member(entry,"deliveryStart",start_v)||
           !met_detail::member(entry,"deliveryEnd",end_v)||
           !met_detail::member(entry,"entryPerArea",per_area)||
           !met_detail::decode_string(start_v,start_t,sizeof(start_t))||
           !met_detail::decode_string(end_v,end_t,sizeof(end_t))||
           !parse_utc_z(start_t,start)||!parse_utc_z(end_t,end))
            return DayAheadError::BadJson;
        // The entry must carry the requested area and nothing else.
        if(!met_detail::member(per_area,area,price_v)) return DayAheadError::WrongArea;
        double price=0;
        if(!np_detail::plain_number(price_v,price)) return DayAheadError::BadValue;
        if(price<kDayAheadMinPerMwh||price>kDayAheadMaxPerMwh)
            return DayAheadError::BadValue;
        if(start%900!=0||end-start!=900) return DayAheadError::BadSlots;
        if(n>0&&start!=out[n-1].end_utc) return DayAheadError::BadSlots;
        out[n++]={start,end,price};
        array.ws();
        if(array.cur>=array.end) return DayAheadError::BadJson;
        if(*array.cur==']') {++array.cur;break;}
        if(*array.cur++!=',') return DayAheadError::BadJson;
    }
    // 23 h (spring), 24 h and 25 h (autumn) market days only. A different
    // length is a truncated or foreign response, never "close enough".
    if(n!=92&&n!=96&&n!=100) return DayAheadError::BadSlots;
    count=n;
    return DayAheadError::Ok;
}
} // namespace enevina
