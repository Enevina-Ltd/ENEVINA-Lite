#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <esp_heap_caps.h>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <esp32s3/rom/miniz.h>
#include <memory>
#include "cottage_bounded_web.hpp"
#include "cottage_dashboard_transport.hpp"
#include "cottage_dashboard_gzip.hpp"

namespace {
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
void send_identity(CottageWebServer& web) {
    const auto* gzip=premium_dashboard_gzip;
    constexpr size_t length=sizeof(premium_dashboard_gzip);
    static_assert(length>18,"gzip header and trailer");
    // The build script emits one deterministic gzip member with no optional
    // fields. Avoid linking a second 0.5 MiB flash copy of the page.
    const size_t total=le32(gzip+length-4);
    if(gzip[0]!=0x1f||gzip[1]!=0x8b||gzip[2]!=8||gzip[3]!=0||
       total==0||total>1024u*1024u) {
        web.send(500,"text/plain","Dashboard data invalid");return;
    }
    // ROM miniz's state and 32 KiB wrapping dictionary belong in PSRAM,
    // never on the 8 KiB web task stack or in the internal TLS heap.
    std::unique_ptr<tinfl_decompressor,decltype(&heap_caps_free)> state(
        static_cast<tinfl_decompressor*>(heap_caps_calloc(1,sizeof(tinfl_decompressor),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> dict(
        static_cast<uint8_t*>(heap_caps_malloc(TINFL_LZ_DICT_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);
    if(!state||!dict){web.send(503,"text/plain","Dashboard temporarily busy");return;}
    tinfl_init(state.get());
    constexpr size_t input_size=length-18;
    size_t consumed=0,produced=0;
    mz_ulong crc=0;
    // Chunked transport uses the same absolute nonblocking write budget.
    // No success terminator is sent if decompression/CRC validation fails.
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200,"text/html; charset=utf-8","");
    for(;;) {
        size_t in=input_size-consumed,out=TINFL_LZ_DICT_SIZE;
        const auto status=tinfl_decompress(state.get(),gzip+10+consumed,&in,
            dict.get(),dict.get(),&out,0);
        if(in>input_size-consumed||out>TINFL_LZ_DICT_SIZE||out>total-produced||
           (status!=TINFL_STATUS_DONE&&status!=TINFL_STATUS_HAS_MORE_OUTPUT)||
           (status==TINFL_STATUS_HAS_MORE_OUTPUT&&out!=TINFL_LZ_DICT_SIZE)) {
            web.abortStream();return;
        }
        consumed+=in;produced+=out;
        crc=mz_crc32(crc,dict.get(),out);
        if(status==TINFL_STATUS_DONE &&
           (consumed!=input_size||produced!=total||uint32_t(crc)!=le32(gzip+length-8))) {
            web.abortStream();return;
        }
        if(out&&!web.streamContent(reinterpret_cast<const char*>(dict.get()),out))return;
        if(status==TINFL_STATUS_DONE) {
            web.streamContent("");return;
        }
        delay(1);
    }
}
}
void cottage_send_dashboard(CottageWebServer& web) {
    web.sendHeader("Vary","Accept-Encoding");
    if(web.acceptsGzip()) {
        web.sendHeader("Content-Encoding","gzip");
        web.send_P(200,"text/html; charset=utf-8",
            reinterpret_cast<PGM_P>(premium_dashboard_gzip),sizeof(premium_dashboard_gzip));
    } else send_identity(web);
}
#endif
