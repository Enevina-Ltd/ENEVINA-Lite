#pragma once
// Region-independent civil time and Nord Pool market-day helpers.
//
// The EU summer-time rule is the same in every member state: it starts at
// 01:00 UTC on the last Sunday of March and ends at 01:00 UTC on the last
// Sunday of October. Only the standard offset differs per country (UTC+0,
// +1, +2). The rule is implemented here without host TZ database or setenv,
// so host tests, ESP32 firmware and every thread see identical answers.
//
// Nord Pool day-ahead delivery dates are CET/CEST calendar dates for EVERY
// bidding zone (a Finnish zone's "2026-11-18" starts at 2026-11-17T23:00Z),
// so the market date is never the user's local date.
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace enevina {
namespace eu_time_detail {
inline bool utc_tm(int64_t utc_seconds,std::tm& out) {
    const std::time_t stamp=static_cast<std::time_t>(utc_seconds);
    if(static_cast<int64_t>(stamp)!=utc_seconds) return false;
#if defined(_WIN32)
    return gmtime_s(&out,&stamp)==0;
#else
    return gmtime_r(&stamp,&out)!=nullptr;
#endif
}
} // namespace eu_time_detail

// True while EU summer time is in force at this UTC instant.
inline bool eu_summer_time(int64_t utc_seconds,bool& summer) {
    std::tm utc{};
    if(!eu_time_detail::utc_tm(utc_seconds,utc)) return false;
    const int month=utc.tm_mon+1;
    summer=(month>3 && month<10);
    if(month==3 || month==10) {
        // Both transition months have 31 days; tm_wday: 0 = Sunday.
        const int weekday31=(utc.tm_wday+31-utc.tm_mday)%7;
        const int last_sunday=31-weekday31;
        const bool after_transition=utc.tm_mday>last_sunday ||
            (utc.tm_mday==last_sunday && utc.tm_hour>=1);
        summer=(month==3) ? after_transition : !after_transition;
    }
    return true;
}

// Local broken-down time for a zone with the given STANDARD offset in whole
// hours (Finland +2, Sweden +1, Ireland/UK/Portugal 0), summer time included.
inline bool eu_local_time(int64_t utc_seconds,int std_offset_hours,std::tm& local) {
    if(std_offset_hours<-1 || std_offset_hours>3) return false;
    bool summer=false;
    if(!eu_summer_time(utc_seconds,summer)) return false;
    return eu_time_detail::utc_tm(
        utc_seconds+(std_offset_hours+(summer?1:0))*3600,local);
}

// Offset from UTC in seconds that applies at this instant.
inline bool eu_utc_offset_seconds(int64_t utc_seconds,int std_offset_hours,
                                  int& offset_seconds) {
    if(std_offset_hours<-1 || std_offset_hours>3) return false;
    bool summer=false;
    if(!eu_summer_time(utc_seconds,summer)) return false;
    offset_seconds=(std_offset_hours+(summer?1:0))*3600;
    return true;
}

// Calendar arithmetic on proleptic Gregorian dates (Howard Hinnant).
inline int64_t days_from_civil_date(int y,unsigned m,unsigned d) {
    y-=m<=2;
    const int era=(y>=0 ? y : y-399)/400;
    const unsigned yoe=static_cast<unsigned>(y-era*400);
    const unsigned doy=(153*(m+(m>2 ? -3 : 9))+2)/5+d-1;
    const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
    return static_cast<int64_t>(era)*146097+doe-719468;
}
inline void civil_from_days_date(int64_t z,int& y,unsigned& m,unsigned& d) {
    z+=719468;
    const int64_t era=(z>=0 ? z : z-146096)/146097;
    const unsigned doe=static_cast<unsigned>(z-era*146097);
    const unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    y=static_cast<int>(yoe)+static_cast<int>(era)*400;
    const unsigned doy=doe-(365*yoe+yoe/4-yoe/100);
    const unsigned mp=(5*doy+2)/153;
    d=doy-(153*mp+2)/5+1;
    m=mp<10 ? mp+3 : mp-9;
    y+=m<=2;
}

// Nord Pool delivery date (CET/CEST calendar date) of the instant, shifted by
// `day_offset` CALENDAR days (not 24 h steps, which break around DST), as
// "YYYY-MM-DD". `out` must hold at least 11 bytes.
inline bool nordpool_market_date(int64_t now_utc,int day_offset,char* out,size_t cap) {
    if(!out || cap<11 || day_offset<-2 || day_offset>2) return false;
    std::tm cet{};
    if(!eu_local_time(now_utc,1,cet)) return false;
    const int64_t days=days_from_civil_date(cet.tm_year+1900,
        static_cast<unsigned>(cet.tm_mon+1),static_cast<unsigned>(cet.tm_mday))+
        day_offset;
    int y=0;unsigned m=0,d=0;
    civil_from_days_date(days,y,m,d);
    const int n=std::snprintf(out,cap,"%04d-%02u-%02u",y,m,d);
    return n==10;
}
} // namespace enevina
