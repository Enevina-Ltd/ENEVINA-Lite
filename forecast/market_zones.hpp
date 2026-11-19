#pragma once
// Countries and electricity bidding zones that Enevina Lite can actually
// price. A zone is listed ONLY when the Nord Pool Data Portal day-ahead
// endpoint returned complete 15-minute data for it in a real request (see
// docs/MARKET_ZONES.md and tests/data/nordpool, fetched 2026-11-19).
// Weather coverage is global and is never used to infer price support.
//
// Currency is the currency of BOTH the requested spot series and the tariff
// components the user types in, so spot and tariff are never mixed across
// currencies and no exchange rate is stored on the device.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enevina {
struct MarketZone {
    const char* id;       // value stored on the device and used by the UI
    const char* np_area;  // Nord Pool deliveryArea parameter
    const char* name_en;  // e.g. "Stockholm"
    const char* name_fi;
};
struct MarketCountry {
    const char* iso;      // ISO 3166-1 alpha-2
    const char* name_en;
    const char* name_fi;
    const char* currency; // ISO 4217, accepted by Nord Pool's currency=
    int8_t std_offset_hours; // standard time; EU summer-time rule applies
    const char* iana;     // display name of the (single) civil time zone
    const MarketZone* zones;
    uint8_t zone_count;
};

namespace zones_detail {
inline constexpr MarketZone fi[]={{"FI","FI","Finland","Suomi"}};
inline constexpr MarketZone se[]={
    {"SE1","SE1","Luleå","Luleå"},{"SE2","SE2","Sundsvall","Sundsvall"},
    {"SE3","SE3","Stockholm","Tukholma"},{"SE4","SE4","Malmö","Malmö"}};
inline constexpr MarketZone no[]={
    {"NO1","NO1","Oslo","Oslo"},{"NO2","NO2","Kristiansand","Kristiansand"},
    {"NO3","NO3","Trondheim","Trondheim"},{"NO4","NO4","Tromsø","Tromsø"},
    {"NO5","NO5","Bergen","Bergen"}};
inline constexpr MarketZone dk[]={
    {"DK1","DK1","West Denmark","Länsi-Tanska"},
    {"DK2","DK2","East Denmark (Copenhagen)","Itä-Tanska (Kööpenhamina)"}};
inline constexpr MarketZone ee[]={{"EE","EE","Estonia","Viro"}};
inline constexpr MarketZone lv[]={{"LV","LV","Latvia","Latvia"}};
inline constexpr MarketZone lt[]={{"LT","LT","Lithuania","Liettua"}};
// Germany and Luxembourg form one bidding zone; Nord Pool names it "GER".
inline constexpr MarketZone de[]={{"DE-LU","GER","Germany / Luxembourg","Saksa / Luxemburg"}};
inline constexpr MarketZone nl[]={{"NL","NL","Netherlands","Alankomaat"}};
inline constexpr MarketZone be[]={{"BE","BE","Belgium","Belgia"}};
inline constexpr MarketZone fr[]={{"FR","FR","France","Ranska"}};
inline constexpr MarketZone pl[]={{"PL","PL","Poland","Puola"}};
inline constexpr MarketZone at[]={{"AT","AT","Austria","Itävalta"}};
inline constexpr MarketZone bg[]={{"BG","BG","Bulgaria","Bulgaria"}};
} // namespace zones_detail

#define ENEVINA_COUNTRY(iso,en,fi,cur,off,iana,arr) \
    {iso,en,fi,cur,off,iana,zones_detail::arr,static_cast<uint8_t>(sizeof(zones_detail::arr)/sizeof(MarketZone))}
inline constexpr MarketCountry market_countries[]={
    ENEVINA_COUNTRY("FI","Finland","Suomi","EUR",2,"Europe/Helsinki",fi),
    ENEVINA_COUNTRY("SE","Sweden","Ruotsi","SEK",1,"Europe/Stockholm",se),
    ENEVINA_COUNTRY("NO","Norway","Norja","NOK",1,"Europe/Oslo",no),
    ENEVINA_COUNTRY("DK","Denmark","Tanska","DKK",1,"Europe/Copenhagen",dk),
    ENEVINA_COUNTRY("EE","Estonia","Viro","EUR",2,"Europe/Tallinn",ee),
    ENEVINA_COUNTRY("LV","Latvia","Latvia","EUR",2,"Europe/Riga",lv),
    ENEVINA_COUNTRY("LT","Lithuania","Liettua","EUR",2,"Europe/Vilnius",lt),
    ENEVINA_COUNTRY("DE","Germany","Saksa","EUR",1,"Europe/Berlin",de),
    ENEVINA_COUNTRY("NL","Netherlands","Alankomaat","EUR",1,"Europe/Amsterdam",nl),
    ENEVINA_COUNTRY("BE","Belgium","Belgia","EUR",1,"Europe/Brussels",be),
    ENEVINA_COUNTRY("FR","France","Ranska","EUR",1,"Europe/Paris",fr),
    ENEVINA_COUNTRY("PL","Poland","Puola","PLN",1,"Europe/Warsaw",pl),
    ENEVINA_COUNTRY("AT","Austria","Itävalta","EUR",1,"Europe/Vienna",at),
    ENEVINA_COUNTRY("BG","Bulgaria","Bulgaria","EUR",2,"Europe/Sofia",bg),
};
#undef ENEVINA_COUNTRY
inline constexpr size_t market_country_count=
    sizeof(market_countries)/sizeof(market_countries[0]);

inline const MarketCountry* find_market_country(const char* iso) {
    if(!iso) return nullptr;
    for(const auto& c:market_countries) if(!std::strcmp(c.iso,iso)) return &c;
    return nullptr;
}
inline const MarketZone* find_market_zone(const MarketCountry& c,const char* id) {
    if(!id) return nullptr;
    for(uint8_t i=0;i<c.zone_count;++i)
        if(!std::strcmp(c.zones[i].id,id)) return &c.zones[i];
    return nullptr;
}
// Country + zone pair is valid only when both are in the supported table.
inline bool valid_market_region(const char* iso,const char* zone_id) {
    const MarketCountry* c=find_market_country(iso);
    return c && find_market_zone(*c,zone_id);
}
// The zone is implied only for single-zone countries; otherwise the user
// must choose (never guess a bidding zone from a city name).
inline const MarketZone* implied_market_zone(const MarketCountry& c) {
    return c.zone_count==1 ? &c.zones[0] : nullptr;
}
} // namespace enevina
