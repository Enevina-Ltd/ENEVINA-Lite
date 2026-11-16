#include "forecast/sources.hpp"
#include <cmath>
#include <cstring>

namespace enevina {
namespace {
bool digit(char c) { return c>='0' && c<='9'; }
bool number(const char* s, size_t pos, size_t length, int& value) {
    value=0;
    for(size_t i=0;i<length;++i) {
        if(!digit(s[pos+i])) return false;
        value=value*10+s[pos+i]-'0';
    }
    return true;
}
bool leap(int y) { return y%4==0 && (y%100!=0 || y%400==0); }
int days_in_month(int y, int m) {
    static constexpr int days[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m]+(m==2 && leap(y));
}
// Howard Hinnant's civil-calendar conversion, with Unix epoch offset.
int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y-=m<=2;
    const int era=(y>=0 ? y : y-399)/400;
    const unsigned yoe=static_cast<unsigned>(y-era*400);
    const unsigned doy=(153*(m+(m>2 ? -3 : 9))+2)/5+d-1;
    const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
    return static_cast<int64_t>(era)*146097+doe-719468;
}
}
bool parse_utc_z(const char* s, int64_t& result) {
    if(!s) return false;
    const size_t length=std::strlen(s);
    if(length<17 || length>32 || s[4]!='-' || s[7]!='-' ||
       s[10]!='T' || s[13]!=':' || s[length-1]!='Z') return false;
    int y,m,d,h,minute,second=0;
    if(!number(s,0,4,y) || !number(s,5,2,m) || !number(s,8,2,d) ||
       !number(s,11,2,h) || !number(s,14,2,minute)) return false;
    size_t cursor=16;
    if(s[cursor]==':') {
        if(cursor+2>=length || !number(s,cursor+1,2,second)) return false;
        cursor+=3;
    }
    if(cursor<length-1 && s[cursor]=='.') {
        ++cursor;
        const size_t first=cursor;
        while(cursor<length-1 && digit(s[cursor])) ++cursor;
        if(cursor==first) return false;
    }
    if(cursor!=length-1 || y<1970 || y>2100 || m<1 || m>12 ||
       d<1 || d>days_in_month(y,m) || h>23 || minute>59 || second>59)
        return false;
    result=days_from_civil(y,static_cast<unsigned>(m),static_cast<unsigned>(d))*86400+
        h*3600+minute*60+second;
    return true;
}
bool normalize_nordpool_fi(const MarketPrice* source, size_t count,
    const Tariff& tariff, PriceSlot* output, size_t capacity) {
    if(!source || !output || count==0 || count>capacity || count>300) return false;
    int64_t previous_end=0;
    for(size_t i=0;i<count;++i) {
        const MarketPrice& p=source[i];
        if(p.start_utc<=0 || p.start_utc%900!=0 ||
           p.end_utc-p.start_utc!=900 ||
           (i>0 && p.start_utc!=previous_end) ||
           !std::isfinite(p.eur_per_mwh)) return false;
        Tariff t=tariff;
        t.spot_eur_per_kwh=p.eur_per_mwh/1000.0;
        t.spot_includes_vat=false;
        double total=0;
        if(!calculate_total_price(t,total)) return false;
        output[i]={p.start_utc,total};
        previous_end=p.end_utc;
    }
    return true;
}
}
