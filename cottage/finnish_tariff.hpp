#pragma once
#include <cstdint>
#include <ctime>

namespace enevina::cottage {
// Civil time in Finland (Europe/Helsinki): UTC+2, summer UTC+3.
// EU DST starts at 01:00 UTC on the last Sunday of March and ends at
// 01:00 UTC on the last Sunday of October. Independent of host TZ rules.
inline bool finnish_local_hour(int64_t utc_seconds, int& hour) {
    const std::time_t stamp=static_cast<std::time_t>(utc_seconds);
    if(static_cast<int64_t>(stamp)!=utc_seconds) return false;
    std::tm utc{};
#if defined(_WIN32)
    if(gmtime_s(&utc,&stamp)!=0) return false;
#else
    if(!gmtime_r(&stamp,&utc)) return false;
#endif
    const int month=utc.tm_mon+1;
    bool summer=(month>3 && month<10);
    if(month==3 || month==10) {
        // Both transition months have 31 days; tm_wday: 0 = Sunday.
        const int weekday31=(utc.tm_wday+31-utc.tm_mday)%7;
        const int last_sunday=31-weekday31;
        const bool after_transition=utc.tm_mday>last_sunday ||
            (utc.tm_mday==last_sunday && utc.tm_hour>=1);
        summer=(month==3) ? after_transition : !after_transition;
    }
    hour=(utc.tm_hour+(summer?3:2))%24;
    return true;
}
inline bool finnish_day_transfer(int64_t utc_seconds, bool& daytime) {
    int hour=0;
    if(!finnish_local_hour(utc_seconds,hour)) return false;
    daytime=hour>=7 && hour<22;
    return true;
}
} // namespace enevina::cottage
