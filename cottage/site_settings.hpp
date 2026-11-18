#pragma once
// Phase-2 site settings, configured from the local browser after Wi-Fi works.
// Pure C++: no NVS, HTTP or Arduino dependency, so validation and the price
// rules are host-tested. The firmware persists the validated struct in NVS.
//
// Units: the HTTP/UI keys carry their unit in the name (c/kWh, %), the stored
// values are EUR/kWh and a 0..1 VAT rate. Timestamps are UTC; the transfer
// schedule is evaluated in Finnish civil time at the 15-minute slot START.
#include "cottage/finnish_tariff.hpp"
#include "cottage/url_guard.hpp"
#include "forecast/pricing.hpp"
#include <cerrno>
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

inline bool valid_site_tariff(const SiteTariff& t) {
    auto in=[](double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    return in(t.margin_eur_per_kwh,0,0.5) && in(t.transfer_day_eur_per_kwh,0,0.5) &&
        in(t.transfer_night_eur_per_kwh,0,0.5) &&
        in(t.electricity_tax_eur_per_kwh,0,0.2) && in(t.vat_rate,0,0.5) &&
        static_cast<uint8_t>(t.kind)<=2 && t.day_start_hour<24 &&
        t.night_start_hour<24 &&
        (t.kind!=TransferKind::TimeOfDay || t.day_start_hour!=t.night_start_hour);
}

// True when the DAY (or General / winter-weekday) transfer price applies.
inline bool site_transfer_is_day(const SiteTariff& t,int64_t utc,bool& day) {
    std::tm local{};
    if(!finnish_local_time(utc,local)) return false;
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
                             double spot_eur_per_kwh,double& total) {
    if(!valid_site_tariff(t) || !std::isfinite(spot_eur_per_kwh)) return false;
    bool day=false;
    if(!site_transfer_is_day(t,slot_start_utc,day)) return false;
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

struct SiteSettings {
    SiteTariff tariff{};
    char cn105_url[128]="";   // ESPHome /events URL, empty = not configured
    char shelly_url[96]="";   // Shelly base URL, empty = not configured
    char fmi_latlon[32]="";   // "lat,lon", empty = no weather forecast
    char pump_adapter[40]=""; // adapter catalog id, empty = not selected
    double min_price_advantage_fraction=0.10;
};

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
    // Same bounds as parse_location() in firmware/main/weather.cpp (Codex P2,
    // PR #30): a location the weather reader rejects must not be saved.
    return parse_decimal(lat,y)&&parse_decimal(lon,x)&&y>=59&&y<=71&&x>=19&&x<=32;
}
} // namespace detail

// Applies ONE key to a copy; the caller validates the whole struct and
// commits atomically, so a rejected form never leaves half-applied values.
inline SettingError apply_site_setting(SiteSettings& s,const char* key,const char* value) {
    if(!key || !value) return SettingError::UnknownKey;
    auto cents=[&](double& field,double max_c)->SettingError {
        double v=0;
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
    return SettingError::UnknownKey;
}
} // namespace enevina::cottage
