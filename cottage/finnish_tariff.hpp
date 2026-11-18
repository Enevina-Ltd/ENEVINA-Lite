#pragma once
#include <cstdint>
#include <ctime>

namespace enevina::cottage {
namespace detail {
inline bool utc_tm(int64_t utc_seconds, std::tm& out) {
    const std::time_t stamp=static_cast<std::time_t>(utc_seconds);
    if(static_cast<int64_t>(stamp)!=utc_seconds) return false;
#if defined(_WIN32)
    return gmtime_s(&out,&stamp)==0;
#else
    return gmtime_r(&stamp,&out)!=nullptr;
#endif
}
} // namespace detail

// Civil time in Finland (Europe/Helsinki): UTC+2, summer UTC+3.
// EU DST starts at 01:00 UTC on the last Sunday of March and ends at
// 01:00 UTC on the last Sunday of October. Independent of host TZ rules.
inline bool finnish_summer_time(int64_t utc_seconds, bool& summer) {
    std::tm utc{};
    if(!detail::utc_tm(utc_seconds,utc)) return false;
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
// Local Finnish broken-down time (tm_mon 0-11, tm_wday 0 = Sunday).
inline bool finnish_local_time(int64_t utc_seconds, std::tm& local) {
    bool summer=false;
    if(!finnish_summer_time(utc_seconds,summer)) return false;
    return detail::utc_tm(utc_seconds+(summer?3:2)*3600,local);
}
inline bool finnish_local_hour(int64_t utc_seconds, int& hour) {
    std::tm local{};
    if(!finnish_local_time(utc_seconds,local)) return false;
    hour=local.tm_hour;
    return true;
}
inline bool finnish_day_transfer(int64_t utc_seconds, bool& daytime) {
    int hour=0;
    if(!finnish_local_hour(utc_seconds,hour)) return false;
    daytime=hour>=7 && hour<22;
    return true;
}
} // namespace enevina::cottage
