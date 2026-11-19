#pragma once
// City search for the first-start wizard and the weather location.
//
// The browser never talks to the geocoder; the device performs the request
// (the phone may be on the offline setup network) and returns a small, already
// validated list. This header holds the host-testable parts only: query
// encoding, the result parser and the filters. HTTPS I/O lives in the
// firmware. Provider: Photon (OpenStreetMap data, ODbL) - see docs/GEOCODING.md
// for the attribution and the commercial-use review that is still required.
//
// Contract: never invent coordinates. A result is accepted only when it has a
// plain name, a two-letter country code and finite in-range coordinates.
#include "forecast/met_norway.hpp" // strict no-allocation JSON scanner
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace enevina {
struct GeoPlace {
    char name[48]{};
    char region[40]{};     // state/county, display only
    char country_iso[3]{};
    char country_name[40]{};
    double lat=0;
    double lon=0;
};
constexpr size_t kGeoMaxQueryBytes=48;
constexpr size_t kGeoMinQueryChars=3;

namespace geo_detail {
inline bool unreserved(unsigned char c) {
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
           c=='-'||c=='_'||c=='.'||c=='~';
}
// Display text: valid UTF-8, no controls and none of the characters that the
// dashboard or a form would treat as markup.
inline bool safe_text(const char* s) {
    const unsigned char* p=reinterpret_cast<const unsigned char*>(s);
    while(*p) {
        const unsigned char c=*p;
        if(c<0x20||c==0x7f||c=='<'||c=='>'||c=='"'||c=='&'||c=='\\'||c=='\'') return false;
        size_t extra=0;
        if(c>=0xF0&&c<=0xF4) extra=3;
        else if(c>=0xE0) extra=2;
        else if(c>=0xC2) extra=1;
        else if(c>=0x80) return false;
        ++p;
        for(size_t i=0;i<extra;++i,++p) if((*p&0xC0)!=0x80) return false;
    }
    return true;
}
inline bool copy(char* dst,size_t cap,std::string_view v) {
    if(v.size()+1>cap) return false;
    std::memcpy(dst,v.data(),v.size());
    dst[v.size()]=0;
    return safe_text(dst);
}
inline bool string_member(std::string_view obj,std::string_view key,char* out,size_t cap) {
    std::string_view v;
    if(!met_detail::member(obj,key,v)) return false;
    if(v.size()<2||v.front()!='"') return false;
    // Escapes are legal JSON but never appear in the names we accept; a name
    // that needs them is dropped rather than half-decoded.
    return met_detail::decode_string(v,out,cap)&&safe_text(out);
}
inline bool number_member(std::string_view obj,std::string_view key,double& out) {
    std::string_view v;
    if(!met_detail::member(obj,key,v)||v.empty()||v.size()>=32) return false;
    met_detail::Json c{v.data(),v.data()+v.size()};
    if(!c.number()) return false;
    c.ws();
    if(c.cur!=c.end) return false;
    char b[32]{};
    std::memcpy(b,v.data(),v.size());
    char* stop=nullptr;
    out=std::strtod(b,&stop);
    return stop!=b&&!*stop&&std::isfinite(out);
}
inline bool lon_lat(std::string_view coords,double& lon,double& lat) {
    met_detail::Json c{coords.data(),coords.data()+coords.size()};
    c.ws();
    if(c.cur>=c.end||*c.cur++!='[') return false;
    double v[2]{};
    for(int i=0;i<2;++i) {
        c.ws();
        const char* from=c.cur;
        if(!c.number()) return false;
        const size_t len=static_cast<size_t>(c.cur-from);
        if(len==0||len>=32) return false;
        char b[32]{};
        std::memcpy(b,from,len);
        char* stop=nullptr;
        v[i]=std::strtod(b,&stop);
        if(stop==b||*stop||!std::isfinite(v[i])) return false;
        c.ws();
        if(i==0) {
            if(c.cur>=c.end||*c.cur++!=',') return false;
        }
    }
    c.ws();
    if(c.cur>=c.end||*c.cur++!=']') return false;
    c.ws();
    if(c.cur!=c.end) return false;
    lon=v[0];
    lat=v[1];
    return true;
}
// OSM place kinds that describe a populated place. Farmyards, single
// dwellings, land use polygons and the like are not a city a user lives in.
inline bool populated_place(std::string_view key,std::string_view value) {
    if(key=="place") {
        for(const char* ok:{"city","town","village","hamlet","municipality",
                            "suburb","quarter","borough","neighbourhood","locality"})
            if(value==ok) return true;
        return false;
    }
    return key=="boundary"&&value=="administrative";
}
} // namespace geo_detail

// Percent-encodes a UTF-8 query. Fails on empty/short/long input and on
// control characters, so nothing unexpected reaches the URL.
inline bool geocode_encode_query(const char* text,char* out,size_t cap) {
    if(!text||!out) return false;
    const size_t n=std::strlen(text);
    if(n==0||n>kGeoMaxQueryBytes) return false;
    size_t chars=0,w=0;
    for(size_t i=0;i<n;++i) {
        const unsigned char c=static_cast<unsigned char>(text[i]);
        if(c<0x20||c==0x7f) return false;
        if((c&0xC0)!=0x80) ++chars;
        if(geo_detail::unreserved(c)) {
            if(w+1>=cap) return false;
            out[w++]=static_cast<char>(c);
        } else {
            if(w+3>=cap) return false;
            out[w++]='%';
            out[w++]="0123456789ABCDEF"[c>>4];
            out[w++]="0123456789ABCDEF"[c&15];
        }
    }
    if(chars<kGeoMinQueryChars) return false;
    out[w]=0;
    return true;
}
inline bool build_geocode_url(const char* query,const char* language,char* out,size_t cap) {
    char encoded[kGeoMaxQueryBytes*3+1];
    if(!geocode_encode_query(query,encoded,sizeof(encoded))) return false;
    if(!language||(std::strcmp(language,"fi")&&std::strcmp(language,"en"))) language="en";
    const int n=std::snprintf(out,cap,
        "https://photon.komoot.io/api/?q=%s&limit=20&lang=%s&layer=city&layer=locality",
        encoded,language);
    return n>0&&static_cast<size_t>(n)<cap;
}
// "lat,lon" with four decimals (~10 m), the format the weather settings use.
inline bool geocode_latlon_text(const GeoPlace& p,char* out,size_t cap) {
    if(!std::isfinite(p.lat)||!std::isfinite(p.lon)||std::fabs(p.lat)>90||
       std::fabs(p.lon)>180) return false;
    const int n=std::snprintf(out,cap,"%.4f,%.4f",p.lat,p.lon);
    return n>0&&static_cast<size_t>(n)<cap;
}

// Parses a Photon FeatureCollection. `only_country` (ISO, may be null)
// restricts results to the country the user already selected. Entries that do
// not meet the contract are skipped, never repaired. Duplicates (same name,
// region and country) are collapsed. `count` is 0 on any structural error.
inline bool parse_geocode(const char* json,size_t length,const char* only_country,
                          GeoPlace* out,size_t capacity,size_t& count) {
    count=0;
    if(!json||!out||capacity==0||length<20||length>48*1024) return false;
    const std::string_view doc(json,length);
    met_detail::Json validator{doc.data(),doc.data()+doc.size()};
    if(!validator.value()) return false;
    validator.ws();
    if(validator.cur!=validator.end) return false;
    std::string_view features;
    if(!met_detail::member(doc,"features",features)) return false;
    met_detail::Json array{features.data(),features.data()+features.size()};
    array.ws();
    if(array.cur>=array.end||*array.cur++!='[') return false;
    size_t n=0;
    for(;;) {
        array.ws();
        if(array.cur>=array.end) {count=0;return false;}
        if(*array.cur==']') break;
        std::string_view feature;
        if(!array.span(feature)) {count=0;return false;}
        std::string_view props,geometry,coords,osm_key,osm_value;
        GeoPlace place{};
        bool ok=met_detail::member(feature,"properties",props)&&
                met_detail::member(feature,"geometry",geometry)&&
                met_detail::member(geometry,"coordinates",coords)&&
                geo_detail::string_member(props,"name",place.name,sizeof(place.name))&&
                geo_detail::string_member(props,"countrycode",place.country_iso,
                                          sizeof(place.country_iso))&&
                place.country_iso[0]&&place.country_iso[1];
        if(ok) {
            for(int i=0;i<2;++i) {
                const char c=place.country_iso[i];
                if(c<'A'||c>'Z') ok=false;
            }
        }
        if(ok) {
            std::string_view key,value;
            ok=met_detail::member(props,"osm_key",key)&&met_detail::member(props,"osm_value",value)&&
               key.size()>2&&value.size()>2&&
               geo_detail::populated_place(key.substr(1,key.size()-2),
                                           value.substr(1,value.size()-2));
        }
        if(ok) {
            // GeoJSON order is [lon, lat]; parsed without any allocation.
            ok=geo_detail::lon_lat(coords,place.lon,place.lat);
        }
        if(ok) {
            ok=ok&&std::isfinite(place.lat)&&std::isfinite(place.lon)&&
               std::fabs(place.lat)<=90&&std::fabs(place.lon)<=180;
        }
        if(ok) {
            // Optional display fields: absence is fine, junk is dropped.
            geo_detail::string_member(props,"country",place.country_name,sizeof(place.country_name));
            if(!geo_detail::string_member(props,"state",place.region,sizeof(place.region)))
                geo_detail::string_member(props,"county",place.region,sizeof(place.region));
            if(only_country&&std::strcmp(only_country,place.country_iso)!=0) ok=false;
        }
        if(ok) {
            for(size_t i=0;i<n;++i)
                if(!std::strcmp(out[i].name,place.name)&&
                   !std::strcmp(out[i].region,place.region)&&
                   !std::strcmp(out[i].country_iso,place.country_iso)) {ok=false;break;}
        }
        if(ok&&n<capacity) out[n++]=place;
        array.ws();
        if(array.cur>=array.end) {count=0;return false;}
        if(*array.cur==']') break;
        if(*array.cur++!=',') {count=0;return false;}
    }
    count=n;
    return true;
}
} // namespace enevina
