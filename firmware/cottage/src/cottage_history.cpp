#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include "cottage_history.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_secure_ota.hpp"
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
constexpr char kFixedModelPath[]="/model1.bin",kVariableModelPath[]="/model2.bin";
LfsFile fixed_model_file(kFixedModelPath),variable_model_file(kVariableModelPath);
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
    // A mount error may mean corrupted *existing* records or failing flash:
    // automatically retrying with formatOnFail=true would destroy them.
    // Format ONLY a verifiably erased, never-written partition. Read the
    // entire dedicated partition; a failed read or any non-0xFF byte forbids
    // formatting. This cannot erase another owner's historical records.
    persistent=LittleFS.begin(false,"/lfs",5,"spiffs");
    if(!persistent) {
        const esp_partition_t* part=esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");
        const bool virgin=part && history_partition_is_blank(part->size,
            [&](size_t off,void* buf,size_t n) {
                return esp_partition_read(part,off,buf,n)==ESP_OK;
            });
        if(virgin) {
            ESP_LOGI(tag,"history volume verified erased: first-use format");
            persistent=LittleFS.begin(true,"/lfs",5,"spiffs");
        } else {
            ESP_LOGE(tag,"history mount failed; preserving nonblank/unknown flash, RAM only");
        }
    }
    if(!persistent) {ESP_LOGE(tag,"history stays in RAM only (mount failed)");return;}
    records_file.remove_stale_temp();
    events_file.remove_stale_temp();
    persistence=new(std::nothrow) HistoryPersistence(records_file,events_file);
    if(!persistence) {persistent=false;return;}
    const LoadResult r=persistence->load(*store,events);
    last_load_records=r.records;
    // Migrate an already-durable UpdateConfirmed event into the independent
    // NVS summary. This includes legacy installs where hist_done was written
    // before hist_last existed: pending_ota is then zero, but the event is
    // still the signed-confirmed history evidence. The OTA transaction
    // service itself verifies good==hist_upg==e.arg before accepting it.
    const uint32_t pending_ota=cottage_ota_recorded_upgrade_version();
    for(size_t i=0;i<events.count();++i) {
        const HistoryEvent& e=events.newest(i);
        if(e.kind==static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed) &&
           e.arg!=0 && (pending_ota==0 || e.arg==pending_ota) &&
           cottage_ota_acknowledge_history_upgrade(e.arg,e.utc))
            break;
    }
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
            // A ring event can be evicted. Ack ONLY after the new event file
            // was durably flushed; this makes release history idempotent even
            // when its original event ages out before the next reboot.
            const uint32_t pending_ota=cottage_ota_recorded_upgrade_version();
            if(pending_ota) {
                for(size_t i=0;i<events.count();++i) {
                    const HistoryEvent& e=events.newest(i);
                    if(e.kind==static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed) &&
                       e.arg==pending_ota) {
                        cottage_ota_acknowledge_history_upgrade(pending_ota,e.utc);
                        break;
                    }
                }
            }
        } else {
            ESP_LOGE(tag,"history flush failed: measurements are RAM-only until retry succeeds");
        }
    }
    xSemaphoreGive(guard);
}

bool history_last_event(uint8_t kind,uint32_t& utc,uint32_t& arg) {
    if(!guard||!store) return false;
    bool found=false;
    xSemaphoreTake(guard,portMAX_DELAY);
    for(size_t i=0;i<events.count();++i) {
        const HistoryEvent& e=events.newest(i);
        if(e.kind==kind) {utc=e.utc;arg=e.arg;found=true;break;}
    }
    xSemaphoreGive(guard);
    return found;
}

CheckpointRead history_model_read(bool variable,uint8_t* data,size_t cap,size_t& used) {
    used=0;
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)
        return CheckpointRead::Error;
    auto& file=variable?variable_model_file:fixed_model_file;
    const auto result=read_model_checkpoint(file,data,cap,used);
    xSemaphoreGive(guard);return result;
}
bool history_model_write(bool variable,const uint8_t* data,size_t used) {
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
    auto& file=variable?variable_model_file:fixed_model_file;
    const bool ok=write_model_checkpoint(file,data,used);
    xSemaphoreGive(guard);return ok;
}
bool history_model_erase(bool variable) {
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
    auto& file=variable?variable_model_file:fixed_model_file;
    const char* path=variable?kVariableModelPath:kFixedModelPath;
    // A leftover temp is never loaded, but clean it before future writes.
    file.remove_stale_temp();
    const bool ok=(!file.exists() || LittleFS.remove(path)) && !file.exists();
    xSemaphoreGive(guard);return ok;
}
bool history_model_present(bool variable) {
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return true;
    auto& file=variable?variable_model_file:fixed_model_file;
    const bool present=file.exists();
    xSemaphoreGive(guard);return present;
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
