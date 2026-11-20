#pragma once
// Phase-2 site settings, configured from the local browser after Wi-Fi works.
// Pure C++: no NVS, HTTP or Arduino dependency, so validation and the price
// rules are host-tested. The firmware persists the validated struct in NVS.
//
// Units: the HTTP/UI keys carry their unit in the name (c/kWh = minor currency
// unit per kWh: cent, ore, grosz; %), the stored values are CURRENCY/kWh of the
// configured region (EUR unless the zone settles in SEK/NOK/DKK/PLN; the
// *_eur_* identifiers predate regional pricing) and a 0..1 VAT rate.
// Timestamps are UTC; the transfer schedule is evaluated in the civil time of
// the selected country (EU summer-time rule) at the 15-minute slot START.
#include "cottage/finnish_tariff.hpp"
#include "cottage/optimization_profile.hpp"
#include "cottage/url_guard.hpp"
#include "forecast/eu_time.hpp"
#include "forecast/market_zones.hpp"
#include "forecast/pricing.hpp"
#include <cerrno>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace enevina::cottage {
// General: one transfer price. TimeOfDay: day price day_start..night_start
// local, otherwise night price (Finnish "yösiirto"). Seasonal: day price on
// Mon-Sat 07-22 from 1 Nov to 31 Mar ("talviarkipäivä"), otherwise the
// "muu aika" price (Finnish "kausisiirto").
enum class TransferKind : uint8_t { General=0, TimeOfDay=1, Seasonal=2 };

struct SiteTariff {
    double margin_eur_per_kwh=0;      // retailer margin on top of spot
    double transfer_day_eur_per_kwh=0;
    double transfer_night_eur_per_kwh=0;
    double electricity_tax_eur_per_kwh=0;
    double vat_rate=0.255;
    // Finnish consumer price lists usually quote margin, transfer and tax
    // INCLUDING VAT. Nord Pool spot is always VAT 0 and gets VAT added once.
    bool components_include_vat=false;
    TransferKind kind=TransferKind::TimeOfDay;
    uint8_t day_start_hour=7;
    uint8_t night_start_hour=22;
};

// Plausibility limits are defined in EUR and scaled for currencies with a
// larger unit count (a SEK/NOK component of 0.60/kWh is normal). They reject
// unit mistakes such as entering cents as whole currency units.
inline double currency_limit_scale(const char* currency) {
    if(!currency || !*currency) return 1.0;
    if(!std::strcmp(currency,"EUR")) return 1.0;
    if(!std::strcmp(currency,"SEK") || !std::strcmp(currency,"NOK")) return 12.0;
    if(!std::strcmp(currency,"DKK")) return 8.0;
    if(!std::strcmp(currency,"PLN")) return 5.0;
    return 1.0;
}
inline bool valid_site_tariff(const SiteTariff& t,double scale=1.0) {
    auto in=[](double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    return in(t.margin_eur_per_kwh,0,0.5*scale) &&
        in(t.transfer_day_eur_per_kwh,0,0.5*scale) &&
        in(t.transfer_night_eur_per_kwh,0,0.5*scale) &&
        in(t.electricity_tax_eur_per_kwh,0,0.2*scale) && in(t.vat_rate,0,0.5) &&
        static_cast<uint8_t>(t.kind)<=2 && t.day_start_hour<24 &&
        t.night_start_hour<24 &&
        (t.kind!=TransferKind::TimeOfDay || t.day_start_hour!=t.night_start_hour);
}

// True when the DAY (or General / winter-weekday) transfer price applies.
inline bool site_transfer_is_day(const SiteTariff& t,int64_t utc,bool& day,
                                 int std_offset_hours=2) {
    std::tm local{};
    if(!enevina::eu_local_time(utc,std_offset_hours,local)) return false;
    const int h=local.tm_hour;
    switch(t.kind) {
    case TransferKind::General: day=true; return true;
    case TransferKind::TimeOfDay:
        day=t.day_start_hour<t.night_start_hour ?
            (h>=t.day_start_hour && h<t.night_start_hour) :
            (h>=t.day_start_hour || h<t.night_start_hour);
        return true;
    case TransferKind::Seasonal: {
        const int month=local.tm_mon+1;
        const bool winter=month>=11 || month<=3;
        const bool mon_sat=local.tm_wday>=1 && local.tm_wday<=6;
        day=winter && mon_sat && h>=7 && h<22;
        return true;
    }
    }
    return false;
}

// Single source of truth for the marginal TOTAL EUR/kWh of a 15-minute slot.
// Every consumer (MPC bridge, price bootstrap, dashboard, audit) uses this.
inline bool site_total_price(const SiteTariff& t,int64_t slot_start_utc,
                             double spot_eur_per_kwh,double& total,
                             int std_offset_hours=2,double limit_scale=1.0) {
    if(!valid_site_tariff(t,limit_scale) || !std::isfinite(spot_eur_per_kwh)) return false;
    bool day=false;
    if(!site_transfer_is_day(t,slot_start_utc,day,std_offset_hours)) return false;
    enevina::Tariff x{};
    x.spot_eur_per_kwh=spot_eur_per_kwh;
    x.margin_eur_per_kwh=t.margin_eur_per_kwh;
    x.transfer_eur_per_kwh=day?t.transfer_day_eur_per_kwh:t.transfer_night_eur_per_kwh;
    x.electricity_tax_eur_per_kwh=t.electricity_tax_eur_per_kwh;
    x.vat_rate=t.vat_rate;
    x.spot_includes_vat=false;
    x.margin_includes_vat=x.transfer_includes_vat=
        x.electricity_tax_includes_vat=t.components_include_vat;
    return enevina::calculate_total_price(x,total);
}

// Where the home is and which market prices apply. Defaults reproduce the
// original Finland-only behaviour, so an upgraded device keeps working. An
// empty price_zone with a non-empty country means "the user still has to pick
// a bidding zone": prices are NOT fetched until it is chosen.
struct SiteSettings {
    SiteTariff tariff{};
    char cn105_url[128]="";   // ESPHome /events URL, empty = not configured
    char shelly_url[96]="";   // Shelly base URL, empty = not configured
    char fmi_latlon[32]="";   // "lat,lon", empty = no weather forecast
    char pump_adapter[40]=""; // adapter catalog id, empty = not selected
    double min_price_advantage_fraction=0.10;
    // --- Regional setup (settings layout v2) ---
    char country[3]="FI";     // ISO 3166-1 alpha-2 of the home
    char price_zone[8]="FI";  // enevina::MarketZone::id, empty = not chosen
    char city[48]="";         // display name chosen from the geocoder
    char timezone[40]="";     // IANA name reported by the geocoder (display)
    char language[3]="";      // "fi"/"en"; empty = follow the browser
    bool tariff_confirmed=false; // user entered/confirmed contract prices
    bool setup_complete=true; // first-run wizard finished (fresh devices: false)
    uint8_t setup_step=0;     // resume point of the wizard
    // Optimization profile and the owner's comfort limits (0 = not set).
    // Balanced without limits behaves exactly like the firmware before profiles.
    OptimizationProfile profile=OptimizationProfile::Balanced;
    double comfort_min_c=0;
    double comfort_max_c=0;
};

// Byte layout of the shipped v1 settings blob (Finland-only, before regional
// setup). It exists ONLY so devices that already hold a configuration keep it
// on upgrade; members and order must never change.
struct SiteSettingsV1 {
    SiteTariff tariff{};
    char cn105_url[128]="";
    char shelly_url[96]="";
    char fmi_latlon[32]="";
    char pump_adapter[40]="";
    double min_price_advantage_fraction=0.10;
};
// An already configured owner keeps every value; the first-start wizard must
// not reappear and the contract they entered counts as confirmed. Region
// fields take the Finland defaults the v1 firmware implicitly used.
inline SiteSettings site_settings_from_v1(const SiteSettingsV1& old) {
    SiteSettings s{};
    s.tariff=old.tariff;
    std::memcpy(s.cn105_url,old.cn105_url,sizeof(s.cn105_url));
    std::memcpy(s.shelly_url,old.shelly_url,sizeof(s.shelly_url));
    std::memcpy(s.fmi_latlon,old.fmi_latlon,sizeof(s.fmi_latlon));
    std::memcpy(s.pump_adapter,old.pump_adapter,sizeof(s.pump_adapter));
    s.min_price_advantage_fraction=old.min_price_advantage_fraction;
    std::strcpy(s.country,"FI");
    std::strcpy(s.price_zone,"FI");
    std::strcpy(s.timezone,"Europe/Helsinki");
    s.tariff_confirmed=true;
    s.setup_complete=true;
    s.setup_step=9;
    return s;
}

// Layout history of the persisted SiteSettings blob (version field stays 2).
// New members are ONLY ever appended, so every older layout is a prefix of the
// current one. A boundary is the offset of the first member a layout added:
// restore_site_settings() copies the largest prefix the stored blob covers and
// leaves everything newer at its default - the owner's saved pump endpoint,
// location, tariff and wizard state survive a firmware upgrade instead of being
// discarded. (Without this an exact-size check silently reset the device.)
constexpr size_t kSettingsLayoutBoundaries[]={offsetof(SiteSettings,profile)};
// Returns false when `size` matches no known layout (corrupt, or written by a
// NEWER firmware that this one cannot interpret): the caller keeps defaults.
inline bool restore_site_settings(const uint8_t* bytes,size_t size,SiteSettings& out) {
    if(!bytes||size>sizeof(SiteSettings)) return false;
    size_t take=0;
    if(size==sizeof(SiteSettings)) take=sizeof(SiteSettings);
    else for(const size_t b:kSettingsLayoutBoundaries) {
        // an older layout's size is its last member rounded up by struct padding
        const size_t padded=(b+alignof(SiteSettings)-1)/alignof(SiteSettings)*alignof(SiteSettings);
        if(size>=b&&size<=padded&&b>take) take=b;
    }
    if(take==0) return false;
    SiteSettings s{};                       // defaults for anything newer than the blob
    std::memcpy(&s,bytes,take);             // never beyond the last whole older member
    out=s;
    return true;
}

// Fingerprint the exact rollback-readable v2 record, including its header
// and padding. A v3 candidate saves this fingerprint alongside its settings.
// If rollback firmware later edits v2, the next upgraded boot must prefer
// those newer owner edits to an older v3 candidate. Missing v2 is signature 0.
inline uint64_t site_v2_signature(const uint8_t* bytes,size_t n) {
    if(!bytes||n==0)return 0;
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<n;++i) {
        h^=bytes[i];
        h*=UINT64_C(1099511628211);
    }
    return h?h:1;
}
inline bool site_rollback_v2_modified(uint64_t v3_baseline,const uint8_t* v2,size_t n) {
    return n>0 && site_v2_signature(v2,n)!=v3_baseline;
}


// Reads a rollback-compatible NVS record without conflating a missing key
// with a transient read failure. Caller supplies bounded scratch storage:
// no dynamic allocation or flash writes while preparing the v4 snapshot.
template<class Nvs>
inline bool site_rollback_snapshot_signature(Nvs& p,const char* key,
                                             uint64_t& signature,uint8_t* scratch,
                                             size_t scratch_size) {
    signature=0;
    const size_t n=p.getBytesLength(key);
    if(n==0) return !p.isKey(key);
    if(!scratch || n>scratch_size) return false;
    if(p.getBytes(key,scratch,n)!=n) return false;
    signature=site_v2_signature(scratch,n);
    return true;
}

// v3's signature-bearing layout contains the previous v2 fingerprint after
// its settings bytes. A first v4 boot MUST honor that provenance before
// preferring the older v3 payload over a v2 edited during rollback.
inline bool site_v3_prefer_rollback_v2(const uint8_t* v3,size_t v3_len,
                                       size_t settings_bytes,
                                       const uint8_t* v2,size_t v2_len) {
    constexpr size_t h=8;
    if(!v3 || v3_len<h || settings_bytes>v3_len-h ||
       v3_len-h-settings_bytes<sizeof(uint64_t)) return false;
    uint64_t baseline=0;
    std::memcpy(&baseline,v3+h+settings_bytes,sizeof(baseline));
    return site_rollback_v2_modified(baseline,v2,v2_len);
}

// Region that prices are fetched for, or false when the user has not finished
// choosing one (unsupported country, or multi-zone country without a zone).
inline bool site_market_region(const SiteSettings& s,const char*& nordpool_area,
                               const char*& currency,int& std_offset_hours) {
    const enevina::MarketCountry* c=enevina::find_market_country(s.country);
    if(!c) return false;
    const enevina::MarketZone* z=enevina::find_market_zone(*c,s.price_zone);
    if(!z) return false;
    nordpool_area=z->np_area;
    currency=c->currency;
    std_offset_hours=c->std_offset_hours;
    return true;
}
// TOTAL price rule bound to the selected region (offset and currency scale).
inline bool site_region_total_price(const SiteSettings& s,int64_t slot_start_utc,
                                    double spot_per_kwh,double& total) {
    // The selected country may have invalidated the previous contract. Spot
    // remains displayable, but spot-only is NOT a confirmed total cost for
    // price-aware optimization. The live controller falls back to thermostat.
    if(!s.tariff_confirmed) return false;
    const enevina::MarketCountry* c=enevina::find_market_country(s.country);
    const int offset=c ? c->std_offset_hours : 2;
    const double scale=currency_limit_scale(c ? c->currency : "EUR");
    return site_total_price(s.tariff,slot_start_utc,spot_per_kwh,total,offset,scale);
}
inline bool valid_site_region(const SiteSettings& s);
// Whole-struct check used before every commit: region and tariff limits agree.
inline bool valid_site_settings(const SiteSettings& s) {
    if(!valid_site_region(s)) return false;
    const enevina::MarketCountry* c=enevina::find_market_country(s.country);
    return static_cast<uint8_t>(s.profile)<=2 &&
        valid_comfort_limits(s.comfort_min_c,s.comfort_max_c) &&
        valid_site_tariff(s.tariff,currency_limit_scale(c ? c->currency : "EUR"));
}
inline bool valid_site_region(const SiteSettings& s) {
    auto terminated=[](const char* p,size_t n){return std::memchr(p,0,n)!=nullptr;};
    if(!terminated(s.country,sizeof(s.country)) ||
       !terminated(s.price_zone,sizeof(s.price_zone)) ||
       !terminated(s.city,sizeof(s.city)) ||
       !terminated(s.timezone,sizeof(s.timezone)) ||
       !terminated(s.language,sizeof(s.language))) return false;
    if(s.setup_step>9) return false;
    if(!s.country[0]) return !s.price_zone[0];
    const enevina::MarketCountry* c=enevina::find_market_country(s.country);
    if(!c) return false;
    if(!s.price_zone[0]) return true;           // zone still to be chosen
    return enevina::find_market_zone(*c,s.price_zone)!=nullptr;
}

enum class SettingError : uint8_t {
    Ok, UnknownKey, BadNumber, OutOfRange, BadUrl, TooLong, BadChoice
};
inline const char* setting_error_name(SettingError e) {
    switch(e) {
    case SettingError::Ok:return "OK";
    case SettingError::UnknownKey:return "UNKNOWN_KEY";
    case SettingError::BadNumber:return "BAD_NUMBER";
    case SettingError::OutOfRange:return "OUT_OF_RANGE";
    case SettingError::BadUrl:return "BAD_URL";
    case SettingError::TooLong:return "TOO_LONG";
    case SettingError::BadChoice:return "BAD_CHOICE";
    }
    return "UNKNOWN";
}

namespace detail {
// Strict decimal: whole string, optional Finnish comma, no hex/inf/nan.
inline bool parse_decimal(const char* s,double& out) {
    if(!s) return false;
    char buf[32];
    const size_t n=std::strlen(s);
    if(n==0 || n>=sizeof(buf)) return false;
    for(size_t i=0;i<=n;++i) {
        const char c=s[i];
        if(c && !((c>='0'&&c<='9')||c=='.'||c==','||c=='-'||c=='+')) return false;
        buf[i]=c==','?'.':c;
    }
    errno=0;
    char* end=nullptr;
    const double v=std::strtod(buf,&end);
    if(errno || end!=buf+n || !std::isfinite(v)) return false;
    out=v;
    return true;
}
inline bool copy_text(char* dst,size_t cap,const char* src) {
    const size_t n=std::strlen(src);
    if(n>=cap) return false;
    std::memcpy(dst,src,n+1);
    return true;
}
// Display text from the geocoder: valid UTF-8, no control characters, no
// markup characters, so it can be echoed into the dashboard safely.
inline bool printable_utf8(const char* s) {
    const unsigned char* p=reinterpret_cast<const unsigned char*>(s);
    while(*p) {
        const unsigned char c=*p;
        if(c<0x20 || c==0x7f || c=='<' || c=='>' || c=='"' || c=='&' || c=='\\') return false;
        size_t extra=0;
        if(c>=0xF0 && c<=0xF4) extra=3;
        else if(c>=0xE0) extra=2;
        else if(c>=0xC2) extra=1;
        else if(c>=0x80) return false;
        ++p;
        for(size_t i=0;i<extra;++i,++p) if((*p&0xC0)!=0x80) return false;
    }
    return true;
}
inline bool iana_name(const char* s) {
    if(!*s) return true;
    const size_t n=std::strlen(s);
    if(n<3 || n>39) return false;
    for(size_t i=0;i<n;++i) {
        const char c=s[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
             c=='/'||c=='_'||c=='-'||c=='+')) return false;
    }
    return true;
}
inline bool valid_latlon(const char* s) {
    if(!*s) return true;
    const char* comma=std::strchr(s,',');
    if(!comma || std::strchr(comma+1,',')) return false;
    char lat[16]{},lon[16]{};
    const size_t a=static_cast<size_t>(comma-s);
    if(a==0 || a>=sizeof(lat) || std::strlen(comma+1)>=sizeof(lon)) return false;
    std::memcpy(lat,s,a);
    std::strcpy(lon,comma+1);
    double y=0,x=0;
    // MET Norway Locationforecast 2.0 covers the world, including equator
    // and prime meridian. Disallow invalid/ambiguous coordinates.
    return parse_decimal(lat,y)&&parse_decimal(lon,x)&&
        y>=-90&&y<=90&&x>=-180&&x<=180;
}
} // namespace detail

// Applies ONE key to a copy; the caller validates the whole struct and
// commits atomically, so a rejected form never leaves half-applied values.
inline SettingError apply_site_setting(SiteSettings& s,const char* key,const char* value) {
    if(!key || !value) return SettingError::UnknownKey;
    // Limits are EUR-based and scaled by the CURRENT country's currency.
    // Order-independent consistency is enforced again by valid_site_settings().
    const enevina::MarketCountry* home=enevina::find_market_country(s.country);
    const double scale=currency_limit_scale(home ? home->currency : "EUR");
    auto cents=[&](double& field,double max_c)->SettingError {
        double v=0;
        max_c*=scale;
        if(!detail::parse_decimal(value,v)) return SettingError::BadNumber;
        if(v<0 || v>max_c) return SettingError::OutOfRange;
        field=v/100.0;
        return SettingError::Ok;
    };
    if(!std::strcmp(key,"margin_c_per_kwh")) return cents(s.tariff.margin_eur_per_kwh,50);
    if(!std::strcmp(key,"transfer_day_c_per_kwh"))
        return cents(s.tariff.transfer_day_eur_per_kwh,50);
    if(!std::strcmp(key,"transfer_night_c_per_kwh"))
        return cents(s.tariff.transfer_night_eur_per_kwh,50);
    if(!std::strcmp(key,"electricity_tax_c_per_kwh"))
        return cents(s.tariff.electricity_tax_eur_per_kwh,20);
    if(!std::strcmp(key,"vat_percent")) {
        double v=0;
        if(!detail::parse_decimal(value,v)) return SettingError::BadNumber;
        if(v<0 || v>50) return SettingError::OutOfRange;
        s.tariff.vat_rate=v/100.0;
        return SettingError::Ok;
    }
    if(!std::strcmp(key,"components_include_vat")) {
        if(!std::strcmp(value,"true")) s.tariff.components_include_vat=true;
        else if(!std::strcmp(value,"false")) s.tariff.components_include_vat=false;
        else return SettingError::BadChoice;
        return SettingError::Ok;
    }
    if(!std::strcmp(key,"transfer_kind")) {
        if(!std::strcmp(value,"general")) s.tariff.kind=TransferKind::General;
        else if(!std::strcmp(value,"time_of_day")) s.tariff.kind=TransferKind::TimeOfDay;
        else if(!std::strcmp(value,"seasonal")) s.tariff.kind=TransferKind::Seasonal;
        else return SettingError::BadChoice;
        return SettingError::Ok;
    }
    if(!std::strcmp(key,"day_start_hour") || !std::strcmp(key,"night_start_hour")) {
        double v=0;
        if(!detail::parse_decimal(value,v)) return SettingError::BadNumber;
        if(v<0 || v>23 || v!=std::floor(v)) return SettingError::OutOfRange;
        (key[0]=='d'?s.tariff.day_start_hour:s.tariff.night_start_hour)=
            static_cast<uint8_t>(v);
        return SettingError::Ok;
    }
    if(!std::strcmp(key,"min_price_advantage_percent")) {
        double v=0;
        if(!detail::parse_decimal(value,v)) return SettingError::BadNumber;
        if(v<0 || v>50) return SettingError::OutOfRange;
        s.min_price_advantage_fraction=v/100.0;
        return SettingError::Ok;
    }
    if(!std::strcmp(key,"cn105_url")) {
        if(*value && !valid_cn105_events_url(value)) return SettingError::BadUrl;
        return detail::copy_text(s.cn105_url,sizeof(s.cn105_url),value)?
            SettingError::Ok:SettingError::TooLong;
    }
    if(!std::strcmp(key,"shelly_url")) {
        if(*value && !valid_shelly_base_url(value)) return SettingError::BadUrl;
        return detail::copy_text(s.shelly_url,sizeof(s.shelly_url),value)?
            SettingError::Ok:SettingError::TooLong;
    }
    if(!std::strcmp(key,"fmi_latlon")) {
        if(!detail::valid_latlon(value)) return SettingError::OutOfRange;
        return detail::copy_text(s.fmi_latlon,sizeof(s.fmi_latlon),value)?
            SettingError::Ok:SettingError::TooLong;
    }
    if(!std::strcmp(key,"pump_adapter")) {
        return detail::copy_text(s.pump_adapter,sizeof(s.pump_adapter),value)?
            SettingError::Ok:SettingError::TooLong; // catalog check by caller
    }
    if(!std::strcmp(key,"country")) {
        if(*value && !enevina::find_market_country(value)) return SettingError::BadChoice;
        char previous_country[3];
        std::memcpy(previous_country,s.country,sizeof(previous_country));
        if(!detail::copy_text(s.country,sizeof(s.country),value)) return SettingError::TooLong;
        // A different country means different currency/VAT/contract: the old
        // amounts would be silently wrong, so they are cleared and must be
        // confirmed again. Everything else (pump, sensors, comfort) is kept.
        if(std::strcmp(previous_country,s.country)!=0 && previous_country[0]) {
            s.tariff.margin_eur_per_kwh=s.tariff.transfer_day_eur_per_kwh=
                s.tariff.transfer_night_eur_per_kwh=
                s.tariff.electricity_tax_eur_per_kwh=0;
            s.tariff.vat_rate=0;
            s.tariff_confirmed=false;
        }
        // Changing country never leaves a zone from another country; a
        // single-zone country implies its zone, otherwise the user picks.
        const enevina::MarketCountry* c=enevina::find_market_country(s.country);
        // Civil time zone follows the country (every supported country has a
        // single zone); the display name is never typed by hand.
        if(c) std::strcpy(s.timezone,c->iana); else s.timezone[0]=0;
        if(c && !enevina::find_market_zone(*c,s.price_zone)) {
            const enevina::MarketZone* only=enevina::implied_market_zone(*c);
            std::strcpy(s.price_zone,only ? only->id : "");
        } else if(!c) s.price_zone[0]=0;
        return SettingError::Ok;
    }
    if(!std::strcmp(key,"price_zone")) {
        if(*value) {
            const enevina::MarketCountry* c=enevina::find_market_country(s.country);
            if(!c || !enevina::find_market_zone(*c,value)) return SettingError::BadChoice;
        }
        return detail::copy_text(s.price_zone,sizeof(s.price_zone),value)?
            SettingError::Ok:SettingError::TooLong;
    }
    if(!std::strcmp(key,"city")) {
        if(!detail::printable_utf8(value)) return SettingError::BadChoice;
        return detail::copy_text(s.city,sizeof(s.city),value)?
            SettingError::Ok:SettingError::TooLong;
    }
    if(!std::strcmp(key,"timezone")) {
        if(!detail::iana_name(value)) return SettingError::BadChoice;
        return detail::copy_text(s.timezone,sizeof(s.timezone),value)?
            SettingError::Ok:SettingError::TooLong;
    }
    if(!std::strcmp(key,"language")) {
        if(*value && std::strcmp(value,"fi") && std::strcmp(value,"en"))
            return SettingError::BadChoice;
        return detail::copy_text(s.language,sizeof(s.language),value)?
            SettingError::Ok:SettingError::TooLong;
    }
    if(!std::strcmp(key,"tariff_confirmed") || !std::strcmp(key,"setup_complete")) {
        bool v=false;
        if(!std::strcmp(value,"true")) v=true;
        else if(std::strcmp(value,"false")) return SettingError::BadChoice;
        (key[0]=='t'?s.tariff_confirmed:s.setup_complete)=v;
        return SettingError::Ok;
    }
    if(!std::strcmp(key,"optimization_profile")) {
        return parse_profile(value,s.profile)?SettingError::Ok:SettingError::BadChoice;
    }
    if(!std::strcmp(key,"comfort_min_c") || !std::strcmp(key,"comfort_max_c")) {
        double v=0;
        if(*value) {
            if(!detail::parse_decimal(value,v)) return SettingError::BadNumber;
            if(v<kComfortMinAllowedC || v>kComfortMaxAllowedC ||
               std::fabs(v*2.0-std::round(v*2.0))>1e-9) return SettingError::OutOfRange;
        }
        (key[9]=='i'?s.comfort_min_c:s.comfort_max_c)=v;   // "" clears the limit
        return SettingError::Ok;
    }
    if(!std::strcmp(key,"setup_step")) {
        double v=0;
        if(!detail::parse_decimal(value,v)) return SettingError::BadNumber;
        if(v<0 || v>9 || v!=std::floor(v)) return SettingError::OutOfRange;
        s.setup_step=static_cast<uint8_t>(v);
        return SettingError::Ok;
    }
    return SettingError::UnknownKey;
}

// Applies a whole form. Region keys (country, price_zone) go first so tariff
// limits are always checked against the NEW currency regardless of the order
// the browser happened to send the fields in. On the first rejected key the
// index is reported and `s` must be discarded by the caller (it works on a
// copy). The final whole-struct validation is still the caller's job.
inline SettingError apply_site_settings_batch(SiteSettings& s,size_t count,
        const char* const* keys,const char* const* values,size_t& bad_index) {
    bad_index=count;
    // Phase 0: country, phase 1: price_zone (needs the country), phase 2: rest.
    for(int phase=0;phase<3;++phase) {
        for(size_t i=0;i<count;++i) {
            if(!keys[i]) continue;
            const int own=!std::strcmp(keys[i],"country") ? 0 :
                          !std::strcmp(keys[i],"price_zone") ? 1 : 2;
            if(own!=phase) continue;
            const SettingError e=apply_site_setting(s,keys[i],values[i]);
            if(e!=SettingError::Ok) {bad_index=i;return e;}
        }
    }
    return SettingError::Ok;
}
} // namespace enevina::cottage
