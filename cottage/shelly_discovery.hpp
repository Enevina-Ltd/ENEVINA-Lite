#pragma once
// Shelly discovery helpers (pure, host-tested). The firmware finds candidates
// with mDNS and fetches two small documents over the home LAN; this header
// validates and classifies what comes back. Lite only READS consumption: the
// relay is never switched (see AGENTS.md F04).
//
// Supported means what the firmware reader really uses today: a Gen2+ device
// without authentication answering /rpc/Switch.GetStatus?id=0 with apower.
// Gen1 devices and devices that require a password are listed and explained,
// never silently accepted.
//
// Fixture note: the documents in the tests follow Shelly's published API
// (Gen1 /shelly, Gen2+ /shelly and Switch.GetStatus); they were NOT recorded
// from a device in this project.
#include "forecast/met_norway.hpp" // strict no-allocation JSON scanner
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace enevina::cottage {
// Construct Shelly's read-only local RPC path without truncation. A valid
// maximum-length LAN address (192.168.255.254) needs 49 bytes including NUL.
inline bool shelly_read_url(const char* base,const char* suffix,char* out,size_t cap) {
    if(!base||!suffix||!out||cap==0)return false;
    const int n=std::snprintf(out,cap,"%s%s",base,suffix);
    return n>0&&static_cast<size_t>(n)<cap;
}

enum class ShellyScope : uint8_t { Unknown=0, HeatPump=1, WholeHouse=2 };
inline const char* shelly_scope_name(ShellyScope s) {
    switch(s) {
    case ShellyScope::Unknown:return "unknown";
    case ShellyScope::HeatPump:return "heat_pump";
    case ShellyScope::WholeHouse:return "whole_house";
    }
    return "unknown";
}
inline bool parse_shelly_scope(const char* t,ShellyScope& out) {
    if(!t) return false;
    if(!std::strcmp(t,"unknown")) out=ShellyScope::Unknown;
    else if(!std::strcmp(t,"heat_pump")) out=ShellyScope::HeatPump;
    else if(!std::strcmp(t,"whole_house")) out=ShellyScope::WholeHouse;
    else return false;
    return true;
}
// Consumption may feed the heat-pump COP/consumption model only when the owner
// has confirmed the meter measures the pump alone.
inline bool shelly_may_feed_pump_model(ShellyScope s) {return s==ShellyScope::HeatPump;}

enum class ShellySupport : uint8_t {
    Supported, Gen1NotSupported, AuthRequired, NotShelly
};
inline const char* shelly_support_name(ShellySupport s) {
    switch(s) {
    case ShellySupport::Supported:return "SUPPORTED";
    case ShellySupport::Gen1NotSupported:return "GEN1_NOT_SUPPORTED";
    case ShellySupport::AuthRequired:return "AUTH_REQUIRED";
    case ShellySupport::NotShelly:return "NOT_SHELLY";
    }
    return "NOT_SHELLY";
}
struct ShellyInfo {
    char id[40]{};      // Gen2+: "shellyplus1pm-a8032abe7d94"; Gen1: type
    char model[40]{};
    char app[24]{};
    int generation=0;   // 1 for Gen1 (no "gen" field)
    bool auth=false;
    ShellySupport support=ShellySupport::NotShelly;
};

namespace shelly_detail {
inline bool text(std::string_view obj,std::string_view key,char* out,size_t cap) {
    std::string_view v;
    if(!met_detail::member(obj,key,v)||v.size()<2||v.front()!='"') return false;
    if(!met_detail::decode_string(v,out,cap)) return false;
    for(const char* p=out;*p;++p) {
        const unsigned char c=static_cast<unsigned char>(*p);
        if(c<0x20||c>0x7e||c=='<'||c=='>'||c=='"'||c=='&'||c=='\\'||c=='\'') return false;
    }
    return true;
}
inline bool number(std::string_view obj,std::string_view key,double& out) {
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
inline bool boolean(std::string_view obj,std::string_view key,bool& out) {
    std::string_view v;
    if(!met_detail::member(obj,key,v)) return false;
    if(v=="true") {out=true;return true;}
    if(v=="false") {out=false;return true;}
    return false;
}
} // namespace shelly_detail

// Parses the /shelly document. Returns false for anything that is not a
// Shelly identification document (so an arbitrary web server on the LAN is
// never offered as a meter).
inline bool parse_shelly_info(const char* json,size_t length,ShellyInfo& out) {
    out=ShellyInfo{};
    if(!json||length<10||length>4096) return false;
    const std::string_view doc(json,length);
    met_detail::Json v{doc.data(),doc.data()+doc.size()};
    if(!v.value()) return false;
    v.ws();
    if(v.cur!=v.end) return false;
    ShellyInfo info{};
    double gen=0;
    if(shelly_detail::number(doc,"gen",gen)) {
        // Gen2+ document
        if(gen<2||gen>9||gen!=std::floor(gen)) return false;
        info.generation=static_cast<int>(gen);
        if(!shelly_detail::text(doc,"id",info.id,sizeof(info.id))||
           std::strncmp(info.id,"shelly",6)!=0) return false;
        shelly_detail::text(doc,"model",info.model,sizeof(info.model));
        shelly_detail::text(doc,"app",info.app,sizeof(info.app));
        if(!shelly_detail::boolean(doc,"auth_en",info.auth)) return false;
        info.support=info.auth?ShellySupport::AuthRequired:ShellySupport::Supported;
    } else {
        // Gen1 document: "type" such as SHSW-PM plus "mac" and "auth".
        if(!shelly_detail::text(doc,"type",info.id,sizeof(info.id))||
           std::strncmp(info.id,"SH",2)!=0) return false;
        char mac[16]{};
        if(!shelly_detail::text(doc,"mac",mac,sizeof(mac))) return false;
        shelly_detail::boolean(doc,"auth",info.auth);
        info.generation=1;
        info.support=ShellySupport::Gen1NotSupported;
    }
    out=info;
    return true;
}

// Switch.GetStatus: apower (W) must be a finite, plausible number.
inline bool parse_shelly_power(const char* json,size_t length,double& watts) {
    if(!json||length<2||length>4096) return false;
    const std::string_view doc(json,length);
    met_detail::Json v{doc.data(),doc.data()+doc.size()};
    if(!v.value()) return false;
    v.ws();
    if(v.cur!=v.end) return false;
    double w=0;
    if(!shelly_detail::number(doc,"apower",w)||w<-100||w>30000) return false;
    watts=w;
    return true;
}

// Candidate filter for mDNS host names ("shellyplus1pm-a8032abe7d94").
inline bool shelly_hostname_candidate(const char* host) {
    if(!host) return false;
    const size_t n=std::strlen(host);
    if(n<8||n>48) return false;
    static const char prefix[]="shelly";
    for(size_t i=0;i<6;++i) {
        char c=host[i];
        if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');
        if(c!=prefix[i]) return false;
    }
    for(size_t i=0;i<n;++i) {
        const char c=host[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')) return false;
    }
    return true;
}

// Only private IPv4 LAN addresses become meter URLs: "http://a.b.c.d".
inline bool shelly_lan_base_url(const uint8_t ip[4],char* out,size_t cap) {
    const bool is_private=ip[0]==10||(ip[0]==172&&ip[1]>=16&&ip[1]<=31)||
        (ip[0]==192&&ip[1]==168)||(ip[0]==169&&ip[1]==254);
    if(!is_private||(ip[0]==0)||(ip[3]==0)||(ip[3]==255)) return false;
    const int n=std::snprintf(out,cap,"http://%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
    return n>0&&static_cast<size_t>(n)<cap;
}
} // namespace enevina::cottage
