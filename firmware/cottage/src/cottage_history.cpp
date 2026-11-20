#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include "cottage_history.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "cottage/history_persist.hpp"
#include "cottage/history_query.hpp"
#include "../../main/market.hpp"

namespace enevina_firmware {
namespace {
using namespace enevina::cottage;
constexpr char tag[]="enevina_hist";
constexpr char kRecordsPath[]="/hist.bin";
constexpr char kEventsPath[]="/events.bin";
constexpr uint32_t kFlushIntervalMs=60UL*60*1000;      // hourly: bounded flash wear
constexpr uint32_t kEventFlushMinMs=10UL*60*1000;       // new events: at most every 10 min

// LittleFS-backed file with a streamed atomic replace (temp file + rename).
class LfsFile final : public FileBackend {
public:
    explicit LfsFile(const char* path):path_(path) {std::snprintf(tmp_,sizeof(tmp_),"%s.tmp",path);}
    bool exists() override {return LittleFS.exists(path_);}
    size_t size() override {
        File f=LittleFS.open(path_,"r");
        if(!f) return 0;
        const size_t n=f.size();
        f.close();
        return n;
    }
    bool read(size_t offset,void* out,size_t n) override {
        File f=LittleFS.open(path_,"r");
        if(!f) return false;
        const bool ok=f.seek(offset) && f.read(static_cast<uint8_t*>(out),n)==n;
        f.close();
        return ok;
    }
    bool append(const void* data,size_t n) override {
        File f=LittleFS.open(path_,"a");
        if(!f) return false;
        const bool ok=f.write(static_cast<const uint8_t*>(data),n)==n;
        f.close();
        return ok;
    }
    bool replace_begin() override {
        if(open_) abort_file();
        LittleFS.remove(tmp_);                       // stale temp from an earlier power cut
        tmpf_=LittleFS.open(tmp_,"w");
        open_=static_cast<bool>(tmpf_);
        return open_;
    }
    bool replace_write(const void* data,size_t n) override {
        return open_ && tmpf_.write(static_cast<const uint8_t*>(data),n)==n;
    }
    bool replace_commit() override {
        if(!open_) return false;
        tmpf_.close();open_=false;
        // littlefs rename over an existing file is atomic: old or new, never half.
        if(LittleFS.rename(tmp_,path_)) return true;
        LittleFS.remove(tmp_);
        return false;
    }
    void replace_abort() override {abort_file();}
    void remove_stale_temp() {LittleFS.remove(tmp_);}
private:
    void abort_file() {
        if(open_) {tmpf_.close();open_=false;}
        LittleFS.remove(tmp_);
    }
    const char* path_;
    char tmp_[24];
    File tmpf_;
    bool open_=false;
};

SemaphoreHandle_t guard=nullptr;
HistoryStore* store=nullptr;
EventLog events;
LfsFile records_file(kRecordsPath),events_file(kEventsPath);
HistoryPersistence* persistence=nullptr;
HistoryRecorder recorder;
bool persistent=false;
bool persistence_healthy=false;  // mounting is not proof that the latest flush succeeded
uint32_t last_flush_ms=0;
uint32_t last_attempt_ms=0;
constexpr uint32_t kRetryIntervalMs=10UL*60*1000;
uint64_t events_total_at_flush=0;
size_t last_load_records=0;
} // namespace

void history_initialize() {
    guard=xSemaphoreCreateMutex();
    if(!guard) {ESP_LOGE(tag,"history mutex allocation failed");return;}
    auto* mem=static_cast<HistoryRecord*>(heap_caps_malloc(
        sizeof(HistoryRecord)*kHistorySlots,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!mem) {ESP_LOGE(tag,"no PSRAM for history; feature disabled");return;}   // fail closed
    store=new(std::nothrow) HistoryStore(mem,kHistorySlots);
    if(!store) {heap_caps_free(mem);return;}
    // Mount the dedicated partition; format it only if it was never formatted.
    // Nothing else (NVS, OTA slots, settings) lives there.
    persistent=LittleFS.begin(false,"/lfs",5,"spiffs");
    if(!persistent) {
        ESP_LOGW(tag,"history partition not formatted yet; formatting it once");
        persistent=LittleFS.begin(true,"/lfs",5,"spiffs");
    }
    if(!persistent) {ESP_LOGE(tag,"history stays in RAM only (mount failed)");return;}
    records_file.remove_stale_temp();
    events_file.remove_stale_temp();
    persistence=new(std::nothrow) HistoryPersistence(records_file,events_file);
    if(!persistence) {persistent=false;return;}
    const LoadResult r=persistence->load(*store,events);
    last_load_records=r.records;
    last_flush_ms=millis();
    last_attempt_ms=last_flush_ms;
    persistence_healthy=true;
    events_total_at_flush=events.total();
    ESP_LOGI(tag,"history loaded: %u records, %u events%s",static_cast<unsigned>(r.records),
             static_cast<unsigned>(r.events),r.record_tail_cut?" (torn tail will be repaired)":"");
}

void history_observe(const HistoryInputs& inputs) {
    if(!guard||!store) return;
    xSemaphoreTake(guard,portMAX_DELAY);
    recorder.observe(inputs,*store,events);
    const uint32_t now=millis();
    const bool hourly=now-last_flush_ms>=kFlushIntervalMs;
    const bool new_events=events.total()!=events_total_at_flush && now-last_flush_ms>=kEventFlushMinMs;
    if(persistent&&persistence&&(hourly||new_events)&&
       (persistence_healthy||now-last_attempt_ms>=kRetryIntervalMs)) {
        // Keep retrying (at most every 10 min after failure), but do NOT mark
        // history durable or advance success bookkeeping on a failed write.
        last_attempt_ms=now;
        const bool ok=persistence->flush(*store,events);
        persistence_healthy=ok;
        if(ok) {
            last_flush_ms=now;
            events_total_at_flush=events.total();
        } else {
            ESP_LOGE(tag,"history flush failed: measurements are RAM-only until retry succeeds");
        }
    }
    xSemaphoreGive(guard);
}

void history_http(WebServer& web) {
    web.sendHeader("Cache-Control","no-store");
    if(!guard||!store) {
        web.send(200,"application/json","{\"valid\":false,\"points\":[],\"events\":[]}");
        return;
    }
    RangeSpec spec{};
    if(!history_range_spec(web.hasArg("range")?web.arg("range").c_str():"24h",spec)) {
        web.send(400,"application/json","{\"valid\":false,\"error\":\"BAD_RANGE\"}");
        return;
    }
    const int64_t now=time(nullptr);
    if(now<=1700000000) {
        web.send(200,"application/json","{\"valid\":false,\"points\":[],\"events\":[]}");
        return;
    }
    auto* pts=static_cast<HistoryPoint*>(heap_caps_malloc(sizeof(HistoryPoint)*kMaxHistoryPoints,
        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto* summary=new(std::nothrow) HistorySummary;
    auto* event_copy=new(std::nothrow) EventLog;
    if(!pts||!summary||!event_copy) {
        heap_caps_free(pts);delete summary;delete event_copy;
        web.send(503,"application/json","{\"valid\":false,\"points\":[],\"events\":[]}");
        return;
    }
    size_t n=0;
    xSemaphoreTake(guard,portMAX_DELAY);
    n=build_history_series(*store,now,spec.range_slots,spec.step_slots,pts,kMaxHistoryPoints,*summary);
    *event_copy=events;
    const bool saved=persistent&&persistence&&persistence_healthy;
    xSemaphoreGive(guard);
    const auto market=market_status();
    const SiteSettings site=site_settings_snapshot();
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200,"application/json; charset=utf-8","");
    char chunk[768];
    size_t used=0;
    history_json(spec.name,spec.step_slots,pts,n,*summary,*event_copy,
        n?pts[0].t:0,market.currency,shelly_scope_name(site.shelly_scope),saved,[&](const char* part) {
            const size_t len=strlen(part);
            if(used+len>=sizeof(chunk)) {chunk[used]=0;web.sendContent(chunk);used=0;delay(1);}
            if(len>=sizeof(chunk)) {web.sendContent(part);return;}
            memcpy(chunk+used,part,len);used+=len;
        });
    if(used) {chunk[used]=0;web.sendContent(chunk);}
    web.sendContent("");
    heap_caps_free(pts);delete summary;delete event_copy;
}
} // namespace enevina_firmware
#endif // COTTAGE_BENCH_MODE
