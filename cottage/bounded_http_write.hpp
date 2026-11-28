#pragma once
// A single absolute, wrap-safe response budget. Progress never restarts it.
// The transport callback MUST be nonblocking: positive = bytes sent,
// zero = transient backpressure, negative = terminal error.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace enevina::cottage {
inline constexpr uint32_t web_response_budget_ms=3500;
inline constexpr size_t web_write_block=4096;
// Large immutable dashboard bodies have their own bounded lifetime.
// The normal 3.5-second synchronous route budget stays unchanged.
inline constexpr uint32_t web_asset_idle_ms=10000;
inline constexpr uint32_t web_asset_total_ms=180000;
inline constexpr unsigned web_asset_writes_per_poll=4;
inline constexpr bool web_asset_expired(uint32_t now,uint32_t started,uint32_t progress) {
    return uint32_t(now-started)>=web_asset_total_ms ||
        uint32_t(now-progress)>=web_asset_idle_ms;
}

template<class Clock,class Send,class Pause>
size_t bounded_http_write(const char* data,size_t size,uint32_t started,uint32_t budget,
                          Clock now,Send send,Pause pause) {
    if(!data || !size)return 0;
    size_t done=0;
    while(done<size && uint32_t(now()-started)<budget) {
        const size_t count=std::min(size-done,web_write_block);
        const int n=send(data+done,count);
        if(n<0 || size_t(n)>count)break;
        if(n>0)done+=size_t(n);
        else pause();
    }
    return done;
}

// Explicit HTTP/1.1 chunk framing. No inherited client.write() for the
// trailing CRLF: all bytes use the SAME response deadline.
template<class Write>
bool bounded_http_chunk(const char* data,size_t size,Write write) {
    if(size && !data)return false;
    char head[2*sizeof(size_t)+3]{};
    const int n=std::snprintf(head,sizeof(head),"%zx\r\n",size);
    if(n<=0 || size_t(n)>=sizeof(head))return false;
    return write(head,size_t(n))==size_t(n) &&
        (!size || write(data,size)==size) && write("\r\n",2)==2;
}
} // namespace enevina::cottage
