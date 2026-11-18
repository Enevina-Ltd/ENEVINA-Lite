#pragma once
// Compact GeoJSON Locationforecast/2.0 temperature parser with no dynamic
// allocations. It does not require building thousands of cJSON nodes on the
// ESP32's scarce INTERNAL heap. Source: MET Norway (CC BY 4.0).
#include "forecast/fmi.hpp"      // Shared OutdoorPoint / interpolation contract
#include "forecast/sources.hpp"  // Strict RFC 3339 UTC timestamp parser
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace enevina {
namespace met_detail {
struct Json {
    const char* cur=nullptr;
    const char* end=nullptr;
    void ws() {while(cur<end && (*cur==' '||*cur=='\n'||*cur=='\r'||*cur=='\t'))++cur;}
    bool quoted(std::string_view& out) {
        ws();
        if(cur>=end||*cur!='"')return false;
        const char* start=++cur;
        while(cur<end) {
            const char c=*cur++;
            if(c=='"') {out={start,static_cast<size_t>((cur-1)-start)};return true;}
            if(static_cast<unsigned char>(c)<32)return false;
            if(c=='\\') {
                if(cur>=end)return false;
                const char esc=*cur++;
                if(!std::strchr("\"\\/bfnrtu",esc))return false;
                if(esc=='u') {
                    for(int i=0;i<4;++i) {
                        if(cur>=end || !((*cur>='0'&&*cur<='9')||
                           (*cur>='a'&&*cur<='f')||
                           (*cur>='A'&&*cur<='F')))return false;
                        ++cur;
                    }
                }
            }
        }
        return false;
    }
    bool number() {
        ws();const char* start=cur;
        if(cur<end&&*cur=='-')++cur;
        if(cur>=end)return false;
        if(*cur=='0')++cur;
        else if(*cur>='1'&&*cur<='9') {
            do {++cur;}while(cur<end&&*cur>='0'&&*cur<='9');
        } else return false;
        if(cur<end&&*cur=='.') {
            ++cur;if(cur>=end||*cur<'0'||*cur>'9')return false;
            do {++cur;}while(cur<end&&*cur>='0'&&*cur<='9');
        }
        if(cur<end&&(*cur=='E'||*cur=='e')){
            ++cur;
            if(cur<end&&(*cur=='-'||*cur=='+'))++cur;
            if(cur>=end||*cur<'0'||*cur>'9')return false;
            do {++cur;}while(cur<end&&*cur>='0'&&*cur<='9');
        }
        return cur>start;
    }
    bool value(int depth=0) {
        if(depth>18)return false;
        ws();
        if(cur>=end)return false;
        if(*cur=='"') {std::string_view ignored;return quoted(ignored);}
        if(*cur=='{' || *cur=='[') {
            const bool object=*cur++=='{';
            const char close=object?'}':']';
            ws();
            if(cur<end&&*cur==close) {++cur;return true;}
            for(;;) {
                if(object) {
                    std::string_view key;
                    if(!quoted(key))return false;
                    ws();if(cur>=end||*cur++!=':')return false;
                }
                if(!value(depth+1))return false;
                ws();
                if(cur>=end)return false;
                if(*cur==close) {++cur;return true;}
                if(*cur++!=',')return false;
            }
        }
        for(const char* literal:{"true","false","null"}) {
            const size_t n=std::strlen(literal);
            if(static_cast<size_t>(end-cur)>=n &&
               std::memcmp(cur,literal,n)==0){cur+=n;return true;}
        }
        return number();
    }
    bool span(std::string_view& out) {
        ws();const char* from=cur;
        if(!value())return false;
        out={from,static_cast<size_t>(cur-from)};
        return true;
    }
};
inline bool member(std::string_view obj,std::string_view key,std::string_view& out) {
    Json c{obj.data(),obj.data()+obj.size()};
    c.ws();if(c.cur>=c.end||*c.cur++!='{')return false;
    bool found=false;
    c.ws();if(c.cur<c.end&&*c.cur=='}') {++c.cur;return false;}
    for(;;) {
        std::string_view name,v;
        if(!c.quoted(name))return false;
        c.ws();if(c.cur>=c.end||*c.cur++!=':')return false;
        if(!c.span(v))return false;
        if(name==key) {
            if(found)return false; // reject duplicate security-sensitive keys
            out=v;found=true;
        }
        c.ws();if(c.cur>=c.end)return false;
        if(*c.cur=='}'){++c.cur;break;}
        if(*c.cur++!=',')return false;
    }
    c.ws();
    return found&&c.cur==c.end;
}
inline bool decode_string(std::string_view source,char* out,size_t cap) {
    Json c{source.data(),source.data()+source.size()};
    std::string_view text;
    if(!c.quoted(text)||text.size()+1>cap)return false;
    c.ws();if(c.cur!=c.end)return false;
    // Time values and identifiers are strictly unescaped ASCII.
    if(text.find('\\')!=std::string_view::npos)return false;
    std::memcpy(out,text.data(),text.size());out[text.size()]='\0';
    return true;
}
inline bool decode_temperature(std::string_view source,double& out) {
    Json c{source.data(),source.data()+source.size()};
    if(!c.number())return false;
    c.ws();if(c.cur!=c.end||source.size()>=32)return false;
    char buffer[32]{};
    std::memcpy(buffer,source.data(),source.size());
    char* stop=nullptr;
    const double value=std::strtod(buffer,&stop);
    if(stop==buffer||*stop||!std::isfinite(value)||value< -60||value>60)
        return false;
    out=value;return true;
}
}
inline bool parse_met_norway_temperature(const char* json,OutdoorPoint* output,
                                          size_t capacity,size_t& count) {
    count=0;
    if(!json||!output||capacity<2)return false;
    const std::string_view doc(json);
    if(doc.size()<40||doc.size()>96*1024)return false;
    // A strict JSON validator is needed for complete GeoJSON input.
    met_detail::Json validator{doc.data(),doc.data()+doc.size()};
    if(!validator.value())return false;
    validator.ws();if(validator.cur!=validator.end)return false;
    std::string_view properties,series;
    if(!met_detail::member(doc,"properties",properties)||
       !met_detail::member(properties,"timeseries",series))return false;
    met_detail::Json array{series.data(),series.data()+series.size()};
    array.ws();if(array.cur>=array.end||*array.cur++!='[')return false;
    for(;;) {
        array.ws();
        if(array.cur>=array.end)return false;
        if(*array.cur==']'){++array.cur;break;}
        std::string_view entry,stamp,data,instant,details,temperature;
        if(!array.span(entry))return false;
        // The first 48+ hours are hourly. Later forecast points may be six
        // hours apart; stop at that transition, never interpolate across it.
        if(count<capacity) {
            if(!met_detail::member(entry,"time",stamp)||
               !met_detail::member(entry,"data",data)||
               !met_detail::member(data,"instant",instant)||
               !met_detail::member(instant,"details",details)||
               !met_detail::member(details,"air_temperature",temperature))
               return false;
            char time_text[40]{};
            int64_t utc=0;double celsius=0;
            if(!met_detail::decode_string(stamp,time_text,sizeof(time_text))||
               !enevina::parse_utc_z(time_text,utc)||
               !met_detail::decode_temperature(temperature,celsius)||
               utc<=0||utc%3600!=0)return false;
            if(count>0) {
                const int64_t step=utc-output[count-1].timestamp_utc;
                if(step!=3600) {
                    // 6-hour MET Norway medium-range switch is not a data
                    // error. But earlier gaps or overlaps are rejected.
                    if(step==21600 && count>=24)break;
                    return false;
                }
            }
            output[count++]={utc,celsius};
        }
        array.ws();
        if(array.cur>=array.end)return false;
        if(*array.cur==']'){++array.cur;break;}
        if(*array.cur++!=',')return false;
    }
    return count>=2;
}
} // namespace enevina
