#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <LittleFS.h>
#include "cottage_bounded_web.hpp"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <atomic>
#include <memory>
#include <new>
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
constexpr uint32_t kFlushIntervalMs=60UL*60*1000;      // hourly fallback for event/repair work
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
        const size_t offset=f.size();
        const bool ok=f.write(static_cast<const uint8_t*>(data),n)==n;
        f.flush();f.close();
        if(!ok || size()!=offset+n)return false;
        // Arduino File::close/flush cannot report all lower-layer errors.
        uint8_t check[128];
        const auto* bytes=static_cast<const uint8_t*>(data);
        for(size_t i=0;i<n;) {
            const size_t k=std::min(sizeof(check),n-i);
            if(!read(offset+i,check,k)||std::memcmp(check,bytes+i,k)!=0)return false;
            i+=k;
        }
        return true;
    }
    bool replace_begin() override {
        if(open_) abort_file();
        LittleFS.remove(tmp_);                       // stale temp from an earlier power cut
        tmpf_=LittleFS.open(tmp_,"w");
        open_=static_cast<bool>(tmpf_);
        replace_size_=0;replace_hash_=14695981039346656037ULL;
        return open_;
    }
    bool replace_write(const void* data,size_t n) override {
        if(!open_ || tmpf_.write(static_cast<const uint8_t*>(data),n)!=n)return false;
        hash_bytes(replace_hash_,static_cast<const uint8_t*>(data),n);
        replace_size_+=n;return true;
    }
    bool replace_commit() override {
        if(!open_) return false;
        tmpf_.flush();tmpf_.close();open_=false;
        // Verify the closed temporary file before replacing the old file.
        File verify=LittleFS.open(tmp_,"r");
        bool ok=verify && verify.size()==replace_size_;
        uint8_t chunk[128];
        uint64_t hash=14695981039346656037ULL;
        size_t remaining=replace_size_;
        while(ok&&remaining) {
            const size_t n=std::min(remaining,sizeof(chunk));
            ok=verify.read(chunk,n)==n;
            if(ok)hash_bytes(hash,chunk,n);
            remaining-=n;
        }
        verify.close();
        // littlefs rename over an existing file is atomic: old or new, never half.
        if(ok && hash==replace_hash_ && LittleFS.rename(tmp_,path_))
            return size()==replace_size_;
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
    static void hash_bytes(uint64_t& hash,const uint8_t* p,size_t n) {
        for(size_t i=0;i<n;++i){hash^=p[i];hash*=1099511628211ULL;}
    }
    size_t replace_size_=0;
    uint64_t replace_hash_=0;
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
constexpr char kMaintenancePath[]="/maint1.bin";
LfsFile maintenance_file(kMaintenancePath);
LfsFile open_slot_file("/histopen1.bin"),baseload_file("/base1.bin");
struct OpenSlotFile {HistoryOpenSlot slot;uint16_t crc;};
bool ota_history_frozen=false; // protected by guard
HistoryPersistence* persistence=nullptr;
HistoryRecorder recorder;
bool persistent=false;
bool persistence_healthy=false;  // mounting is not proof that the latest flush succeeded
uint32_t last_flush_ms=0;
uint32_t last_attempt_ms=0;
constexpr uint32_t kRetryIntervalMs=10UL*60*1000;
uint64_t events_total_at_flush=0;
size_t last_load_records=0;
std::atomic<uint32_t> history_busy_reads{0};
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
    maintenance_file.remove_stale_temp();
    open_slot_file.remove_stale_temp();
    baseload_file.remove_stale_temp();
    persistence=new(std::nothrow) HistoryPersistence(records_file,events_file);
    if(!persistence) {persistent=false;return;}
    const LoadResult r=persistence->load(*store,events);
    last_load_records=r.records;
    OpenSlotFile open{};
    if(open_slot_file.size()==sizeof(open) && open_slot_file.read(0,&open,sizeof(open)) &&
       open.crc==history_crc16(reinterpret_cast<const uint8_t*>(&open.slot),sizeof(open.slot)))
        recorder.restore_open(open.slot,*store); // stale or already finalized is rejected
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
    if(ota_history_frozen){xSemaphoreGive(guard);return;}
    const bool finalized=recorder.observe(inputs,*store,events);
    const uint32_t now=millis();
    const bool hourly=now-last_flush_ms>=kFlushIntervalMs;
    const bool new_events=events.total()!=events_total_at_flush && now-last_flush_ms>=kEventFlushMinMs;
    if(persistent&&persistence&&(finalized||hourly||new_events)&&
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

// OTA must never select a new boot partition while completed records are
// known to be RAM-only. Same mutex and persistence transaction as recorder.
bool history_flush_for_ota() {
    if(!guard||!store||!persistent||!persistence)return false;
    if(xSemaphoreTake(guard,pdMS_TO_TICKS(10000))!=pdTRUE)return false;
    bool ok=persistence->flush(*store,events);
    OpenSlotFile open{};
    recorder.snapshot_open(open.slot);
    open.crc=history_crc16(reinterpret_cast<const uint8_t*>(&open.slot),sizeof(open.slot));
    if(ok)ok=write_model_checkpoint(open_slot_file,reinterpret_cast<const uint8_t*>(&open),sizeof(open));
    persistence_healthy=ok;
    last_attempt_ms=millis();
    if(ok){
        last_flush_ms=last_attempt_ms;events_total_at_flush=events.total();
        // Stop new history observations only for the short activation/reboot
        // interval. The stored accumulator carries the unfinished quarter.
        ota_history_frozen=true;
    }
    xSemaphoreGive(guard);
    return ok;
}

void history_resume_after_ota() {
    if(!guard)return;
    xSemaphoreTake(guard,portMAX_DELAY);
    ota_history_frozen=false;
    xSemaphoreGive(guard);
}
bool history_last_event(uint8_t kind,uint32_t& utc,uint32_t& arg) {
    if(!guard||!store) return false;
    bool found=false;
    // Optional dashboard/OTA evidence must not wait behind LittleFS
    // compaction or a model checkpoint. A busy lookup is unknown, not absent.
    if(xSemaphoreTake(guard,0)!=pdTRUE) {
        history_busy_reads.fetch_add(1);return false;
    }
    for(size_t i=0;i<events.count();++i) {
        const HistoryEvent& e=events.newest(i);
        if(e.kind==kind) {utc=e.utc;arg=e.arg;found=true;break;}
    }
    xSemaphoreGive(guard);
    return found;
}

uint32_t history_read_busy_count(){return history_busy_reads.load();}

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
    bool ok=(!file.exists() || LittleFS.remove(path)) && !file.exists();
    if(!variable) {
        maintenance_file.remove_stale_temp();
        const bool cycle_erased=(!maintenance_file.exists() || LittleFS.remove(kMaintenancePath)) &&
            !maintenance_file.exists();
        ok=ok && cycle_erased;
    }
    xSemaphoreGive(guard);return ok;
}
bool history_model_present(bool variable) {
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return true;
    auto& file=variable?variable_model_file:fixed_model_file;
    const bool present=file.exists();
    xSemaphoreGive(guard);return present;
}

CheckpointRead history_maintenance_read(uint8_t* data,size_t cap,size_t& used) {
    used=0;
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)
        return CheckpointRead::Error;
    const auto result=read_model_checkpoint(maintenance_file,data,cap,used);
    xSemaphoreGive(guard);return result;
}
bool history_maintenance_write(const uint8_t* data,size_t used) {
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
    const bool ok=write_model_checkpoint(maintenance_file,data,used);
    xSemaphoreGive(guard);return ok;
}

CheckpointRead history_baseload_read(uint8_t* data,size_t cap,size_t& used) {
    used=0;
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)
        return CheckpointRead::Error;
    const auto result=read_model_checkpoint(baseload_file,data,cap,used);
    xSemaphoreGive(guard);return result;
}
bool history_baseload_write(const uint8_t* data,size_t used) {
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
    const bool ok=write_model_checkpoint(baseload_file,data,used);
    xSemaphoreGive(guard);return ok;
}
bool history_maintenance_present() {
    if(!persistent || !guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return true;
    const bool present=maintenance_file.exists();
    xSemaphoreGive(guard);return present;
}

void history_http(CottageWebServer& web) {
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
    // Points, event copy, summary and JSON chunk all belong to this
    // response. Do not consume scarce internal heap or loop-task stack.
    struct Scratch {
        HistoryPoint points[kMaxHistoryPoints];
        HistorySummary summary;
        EventLog events;
        char chunk[768];
    };
    auto* mem=heap_caps_malloc(sizeof(Scratch),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!mem) {
        web.send(503,"application/json","{\"valid\":false,\"points\":[],\"events\":[]}");
        return;
    }
    struct ReleaseScratch {
        void operator()(Scratch* p)const {p->~Scratch();heap_caps_free(p);}
    };
    std::unique_ptr<Scratch,ReleaseScratch> scratch(new(mem) Scratch{});
    auto* pts=scratch->points;
    auto* summary=&scratch->summary;
    auto* event_copy=&scratch->events;
    size_t n=0;
    // The same guard serializes LittleFS flush/compaction and model
    // checkpoint I/O. Never block the Arduino loop on that work.
    if(xSemaphoreTake(guard,0)!=pdTRUE) {
        history_busy_reads.fetch_add(1);
        web.sendHeader("Retry-After","1");
        web.send(503,"application/json","{\"valid\":false,\"error\":\"HISTORY_BUSY\",\"points\":[],\"events\":[]}");
        return;
    }
    n=build_history_series(*store,now,spec.range_slots,spec.step_slots,pts,kMaxHistoryPoints,*summary);
    *event_copy=events;
    const bool saved=persistent&&persistence&&persistence_healthy;
    xSemaphoreGive(guard);
    const auto market=market_status();
    const SiteSettings site=site_settings_snapshot();
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200,"application/json; charset=utf-8","");
    auto& chunk=scratch->chunk;
    size_t used=0;
    history_json(spec.name,spec.step_slots,pts,n,*summary,*event_copy,
        n?pts[0].t:0,market.currency,shelly_scope_name(site.shelly_scope),saved,[&](const char* part) {
            if(web.responseAborted())return;
            const size_t len=strlen(part);
            if(used+len>=sizeof(chunk)) {chunk[used]=0;web.streamContent(chunk);used=0;delay(1);}
            if(len>=sizeof(chunk)) {web.streamContent(part);return;}
            memcpy(chunk+used,part,len);used+=len;
        });
    if(used) {chunk[used]=0;web.streamContent(chunk);}
    web.streamContent("");
}
} // namespace enevina_firmware
#endif // COTTAGE_BENCH_MODE
