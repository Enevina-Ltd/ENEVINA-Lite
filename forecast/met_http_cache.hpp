#pragma once
// RFC 7234 / MET Norway response policy. Strict UTC Expires and 304 handling.
// All time computations are time-zone independent and allocation-free.
#include <cstdint>
#include <cstring>
namespace enevina::met {
inline int digit(char c){return c>='0'&&c<='9'?c-'0':-1;}
inline int number2(const char*p){
    const int a=digit(p[0]),b=digit(p[1]);return a<0||b<0?-1:10*a+b;
}
inline int number4(const char*p){
    const int a=number2(p),b=number2(p+2);return a<0||b<0?-1:100*a+b;
}
inline bool leap(int year){return year%4==0&&(year%100!=0||year%400==0);}
inline int days_in_month(int year,int month){
    constexpr int days[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    return month==2 ? 28+int(leap(year)):days[month];
}
// Howard Hinnant civil date to days relative to 1970-01-01.
inline int64_t civil_days(int year,int month,int day) {
    year-=month<=2;
    const int era=(year>=0?year:year-399)/400;
    const unsigned y=static_cast<unsigned>(year-era*400);
    const unsigned doy=(153*static_cast<unsigned>(month+(month>2?-3:9))+2)/5+
                       static_cast<unsigned>(day)-1;
    const unsigned doe=y*365+y/4-y/100+doy;
    return static_cast<int64_t>(era)*146097+doe-719468;
}
inline int64_t expires_utc(const char* text) {
    // Example: Wed, 18 Nov 2026 21:39:50 GMT
    if(!text||std::strlen(text)!=29||text[3]!=','||text[4]!=' '||
       text[7]!=' '||text[11]!=' '||text[16]!=' '||text[19]!=':'||
       text[22]!=':'||text[25]!=' '||std::memcmp(text+26,"GMT",3)!=0)
       return 0;
    constexpr const char* months[]={"Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"};
    int m=0;
    for(int i=0;i<12;++i)if(std::memcmp(text+8,months[i],3)==0){m=i+1;break;}
    const int day=number2(text+5),year=number4(text+12);
    const int hour=number2(text+17),min=number2(text+20),sec=number2(text+23);
    if(!m||year<1970||year>2200||day<1||day>days_in_month(year,m)||
       hour<0||hour>23||min<0||min>59||sec<0||sec>59)return 0;
    return civil_days(year,m,day)*86400+hour*3600+min*60+sec;
}
inline int64_t min_safe_retry(int64_t now,int64_t api_expires) {
    // Missing/invalid Expires: do not hammer an open service.
    if(api_expires<now)return now+3600;
    return api_expires;
}
}
