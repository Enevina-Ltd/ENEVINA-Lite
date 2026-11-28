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
    // Inflate once into bounded external memory. The exact fixed-length
    // body is then sent nonblocking over subsequent web-loop iterations.
    // Avoid a 3.5-second synchronous response timeout on weak Wi-Fi.
    std::unique_ptr<tinfl_decompressor,decltype(&heap_caps_free)> state(
        static_cast<tinfl_decompressor*>(heap_caps_calloc(1,sizeof(tinfl_decompressor),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> page(
        static_cast<uint8_t*>(heap_caps_malloc(total,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);
    if(!state||!page){web.send(503,"text/plain","Dashboard temporarily busy");return;}
    tinfl_init(state.get());
    constexpr size_t input_size=length-18;
    size_t in=input_size,out=total;
    const auto status=tinfl_decompress(state.get(),gzip+10,&in,
        page.get(),page.get(),&out,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if(status!=TINFL_STATUS_DONE || in!=input_size || out!=total ||
       uint32_t(mz_crc32(0,page.get(),out))!=le32(gzip+length-8)) {
        web.send(500,"text/plain","Dashboard data invalid");return;
    }
    auto* data=page.release();
    web.deferAsset("text/html; charset=utf-8",data,total,data);
}
}
void cottage_send_dashboard(CottageWebServer& web) {
    web.sendHeader("Vary","Accept-Encoding");
    if(web.assetBusy()){web.send(503,"text/plain","Dashboard temporarily busy");return;}
    if(web.acceptsGzip()) {
        web.sendHeader("Content-Encoding","gzip");
        web.deferAsset("text/html; charset=utf-8",
            premium_dashboard_gzip,sizeof(premium_dashboard_gzip));
    } else send_identity(web);
}
#endif
