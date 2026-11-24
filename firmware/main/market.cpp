#ifdef ARDUINO
#include "tls_roots.hpp"
#include "esp_heap_caps.h"
#ifdef COTTAGE_SHARED_MEMORY_GUARD
#include "cottage_network_budget.hpp"
#endif
#endif
#include "market.hpp"
#include "market_cache.hpp"
#include "market_cache_compact.hpp"
#include "forecast_memory.hpp"
#include "forecast/sources.hpp"
#include "forecast/eu_time.hpp"
#include "forecast/market_zones.hpp"
#include "forecast/nordpool_day_ahead.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <memory>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#ifndef ARDUINO
#include "esp_netif_sntp.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

namespace enevina_firmware {
namespace {
constexpr char tag[]="enevina_market";
constexpr size_t max_slots=price_cache_max_slots;
constexpr uint32_t cache_version=price_cache_version;
ForecastBuffer<PriceCache> cache;
SemaphoreHandle_t mutex=nullptr;
TaskHandle_t reader_task=nullptr; // notify on live region change, no new watchdog

// Region the cache belongs to. Default keeps Finland/EUR so an unconfigured
// or upgraded device behaves exactly as before. Guarded by `mutex`.
char region_area[8]="FI";
char region_currency[4]="EUR";
uint32_t region_generation=0;
uint32_t cache_revision=0;
uint8_t last_error=0;          // enevina::DayAheadError of the last attempt
int last_http_status=0;
int64_t last_attempt_utc=0;
constexpr char region_key[]="price_region";
// A rollback firmware reads price_v2 unconditionally as FI/EUR. Never place
// SE/NO/DK/PLN or any other foreign market into that legacy NVS key.
constexpr char regional_cache_key[]="price_reg_v2";

bool valid_prices(const PriceCache& candidate) {
    // Same normalization predicate, without allocating a second full price array.
    if(candidate.version!=cache_version || candidate.count==0 || candidate.count>max_slots)
        return false;
    for(size_t i=0;i<candidate.count;++i) {
        enevina::PriceSlot normalized{};
        const auto& point=candidate.prices[i];
        if(point.start_utc<=0 || point.start_utc>INT64_MAX-900 ||
           point.end_utc!=point.start_utc+900)return false;
        if(!enevina::normalize_nordpool_fi(candidate.prices.data()+i,1,
                enevina::Tariff{},&normalized,1) ||
           (i && candidate.prices[i-1].end_utc!=candidate.prices[i].start_utc))
            return false;
    }
    return true;
}

bool same_prices(const PriceCache& a, const PriceCache& b) {
    if(a.count!=b.count) return false;
    for(size_t i=0;i<a.count;++i) {
        if(a.prices[i].start_utc!=b.prices[i].start_utc ||
           a.prices[i].end_utc!=b.prices[i].end_utc ||
           a.prices[i].eur_per_mwh!=b.prices[i].eur_per_mwh) return false;
    }
    return true;
}

struct MarketIdentity {
    char area[8]{},currency[4]{};
    uint32_t generation=0;
};
MarketIdentity current_identity() {
    MarketIdentity id{};
    xSemaphoreTake(mutex,portMAX_DELAY);
    std::snprintf(id.area,sizeof(id.area),"%s",region_area);
    std::snprintf(id.currency,sizeof(id.currency),"%s",region_currency);
    id.generation=region_generation;
    xSemaphoreGive(mutex);
    return id;
}
bool identity_current_locked(const MarketIdentity& id) {
    return id.area[0] && id.generation==region_generation &&
        std::strcmp(id.area,region_area)==0 && std::strcmp(id.currency,region_currency)==0;
}
bool erase_price_key(nvs_handle_t handle,const char* key) {
    const auto err=nvs_erase_key(handle,key);
    return err==ESP_OK||err==ESP_ERR_NVS_NOT_FOUND;
}
bool legacy_finland(const char* area,const char* currency) {
    return std::strcmp(area,"FI")==0 && std::strcmp(currency,"EUR")==0;
}
bool save_cache(const PriceCache& value,const MarketIdentity& id) {
    auto packed=forecast_buffer<RegionalCompactPriceCache>();
    if(!packed || !pack_regional_price_cache(value,id.area,id.currency,*packed))return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const bool current=identity_current_locked(id);
    xSemaphoreGive(mutex);
    if(!current)return false;
    nvs_handle_t handle;
    if(nvs_open("enevina_market",NVS_READWRITE,&handle)!=ESP_OK)return false;
    // Only this reader writes price blobs. A region switch no longer erases
    // keys asynchronously. A late write may lose a cache on next boot,
    // but the embedded identity cannot label it as another market.
    const bool fi=legacy_finland(id.area,id.currency);
    // If an older pre-regional-migration build left a foreign/invalid
    // stamp, remove its ambiguous price_v2 BEFORE clearing the stamp.
    // A power cut must not turn those bytes into a Finnish cache.
    char previous_stamp[16]{};size_t previous_size=sizeof(previous_stamp);
    const auto previous_err=nvs_get_str(handle,region_key,previous_stamp,&previous_size);
    const bool prior_fi=previous_err==ESP_ERR_NVS_NOT_FOUND ||
        (previous_err==ESP_OK && std::strcmp(previous_stamp,"FI/EUR")==0);
    if(fi && !prior_fi && (!erase_price_key(handle,"price_v2") || nvs_commit(handle)!=ESP_OK)) {
        nvs_close(handle);return false;
    }
    const bool cleared=erase_price_key(handle,"price_v1") &&
        erase_price_key(handle,fi?regional_cache_key:"price_v2") &&
        erase_price_key(handle,region_key) && nvs_commit(handle)==ESP_OK;
    // Finland-only legacy key is NEVER used for other regions. The regional
    // key holds a self-identifying v3 envelope, not a separate region stamp.
    const bool ok=cleared && (fi?
        nvs_set_blob(handle,"price_v2",&packed->data,sizeof(packed->data)):
        nvs_set_blob(handle,regional_cache_key,packed.get(),sizeof(*packed)))==ESP_OK &&
        nvs_commit(handle)==ESP_OK;
    nvs_close(handle);
    if(!ok)ESP_LOGW(tag,"Price cache persistence failed; retaining RAM");
    return ok;
}
void load_cache() {
    const auto wanted=current_identity();
    if(!wanted.area[0])return;
    auto saved=forecast_buffer<PriceCache>();
    auto packed=forecast_buffer<RegionalCompactPriceCache>();
    if(!saved||!packed)return;
    nvs_handle_t handle;
    if(nvs_open("enevina_market",NVS_READONLY,&handle)!=ESP_OK)return;
    bool migrate_legacy=false,ok=false;
    const bool fi=legacy_finland(wanted.area,wanted.currency);
    if(fi) {
        // A genuinely absent stamp predates regional pricing. Other errors
        // and non-FI stamps cannot authorize the Finland legacy key.
        char stamp[16]{};size_t stamp_size=sizeof(stamp);
        const auto err=nvs_get_str(handle,region_key,stamp,&stamp_size);
        const bool fi_stamp=err==ESP_ERR_NVS_NOT_FOUND ||
            (err==ESP_OK && std::strcmp(stamp,"FI/EUR")==0);
        if(fi_stamp) {
            size_t size=sizeof(packed->data);
            ok=nvs_get_blob(handle,"price_v2",&packed->data,&size)==ESP_OK &&
                size==sizeof(packed->data) && unpack_price_cache(packed->data,*saved) &&
                valid_prices(*saved);
            if(!ok) {
                size=sizeof(*saved);
                ok=nvs_get_blob(handle,"price_v1",saved.get(),&size)==ESP_OK &&
                    size==sizeof(*saved) && valid_prices(*saved);
                migrate_legacy=ok;
            }
        }
    } else {
        size_t size=sizeof(*packed);
        // Old regional v2 had a separate stamp which could survive a torn
        // write. Its provenance cannot be recovered: fetch once instead.
        // Never fall back to the Finland legacy blob for a foreign region.
        ok=nvs_get_blob(handle,regional_cache_key,packed.get(),&size)==ESP_OK &&
            size==sizeof(*packed) &&
            unpack_regional_price_cache(*packed,wanted.area,wanted.currency,*saved) &&
            valid_prices(*saved);
    }
    nvs_close(handle);
    if(!ok)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const bool accepted=identity_current_locked(wanted);
    if(accepted) {
        *cache=*saved;++cache_revision;
    }
    xSemaphoreGive(mutex);
    if(accepted&&migrate_legacy)save_cache(*saved,wanted);
}

// Fetches and strictly parses ONE Nord Pool market day for the given area and
// currency. `code` is the enevina::DayAheadError (or 100+ for transport).
bool fetch_day(const char* date,const char* area,const char* currency,
    enevina::MarketPrice* output,size_t capacity,size_t& count,uint8_t& code,
    int& http_status) {
    using enevina::DayAheadError;
    count=0;
    code=static_cast<uint8_t>(DayAheadError::Ok);
    http_status=0;
#ifdef COTTAGE_SHARED_MEMORY_GUARD
    // Never run Nord Pool and weather TLS handshakes together or during BLE
    // discovery. Reject unsafe load rather than exhausting internal RAM.
    NetworkBudgetLease network_lease(8000, 78*1024, 16*1024);
    if(!network_lease.held()) {code=101;return false;}
#endif
    char url[220];
    if(!enevina::build_day_ahead_url(area,currency,date,url,sizeof(url))) {
        code=static_cast<uint8_t>(DayAheadError::BadRequest);
        return false;
    }
    esp_http_client_config_t cfg{};
    cfg.url=url;
    cfg.timeout_ms=10000;
    #ifdef ARDUINO
    cfg.cert_pem=enevina_firmware::nordpool_ca_pem;
    #else
    cfg.crt_bundle_attach=esp_crt_bundle_attach;
    #endif
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client) {code=102;return false;}
    bool ok=false;
    code=103; // transport failure until proven otherwise
    char* body=nullptr;
    if(esp_http_client_open(client,0)==ESP_OK) {
#ifdef COTTAGE_SHARED_MEMORY_GUARD
        network_budget_sample();
#endif
        esp_http_client_fetch_headers(client);
        http_status=esp_http_client_get_status_code(client);
        // 204 = the market has not published this day for this area (yet).
        if(http_status==204) code=104;
        if(http_status==200) {
            constexpr size_t limit=48000;
            #ifdef ARDUINO
            // BLE and two concurrent HTTPS readers share scarce internal
            // RAM. Download bodies must use verified external PSRAM, and
            // fail closed if unavailable (no internal-RAM fallback).
            body=static_cast<char*>(heap_caps_malloc(
                limit+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
            #else
            body=static_cast<char*>(std::malloc(limit+1));
            #endif
            if(body) {
                size_t used=0;
                int read=0;
                while(used<limit && (read=esp_http_client_read(client,body+used,
                    static_cast<int>(limit-used)))>0) used+=static_cast<size_t>(read);
#ifdef COTTAGE_SHARED_MEMORY_GUARD
                network_budget_sample();
#endif
                if(read==0 && used>0) {
                    body[used]='\0';
                    // No-allocation strict parser: no cJSON tree on the heap.
                    const DayAheadError e=enevina::parse_day_ahead(body,used,area,
                        currency,output,capacity,count);
                    code=static_cast<uint8_t>(e);
                    ok=e==DayAheadError::Ok;
                    if(!ok) count=0;
                }
            }
        }
        esp_http_client_close(client);
    }
    std::free(body);
    esp_http_client_cleanup(client);
    return ok;
}

void poll_prices(void*) {
    // Market dates are CET/CEST calendar dates for every bidding zone
    // (enevina::nordpool_market_date); slots themselves stay UTC. No process
    // TZ is needed or changed here.
    #ifndef ARDUINO
    esp_sntp_config_t sntp=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
    #endif
    load_cache();
    while(true) {
        const time_t now=time(nullptr);
        bool updated=false;
        if(now>1700000000) {
            // Snapshot the region this attempt belongs to. A region change
            // while the request is in flight makes the whole result void.
            char area[8],currency[4];
            uint32_t generation=0;
            xSemaphoreTake(mutex,portMAX_DELAY);
            std::snprintf(area,sizeof(area),"%s",region_area);
            std::snprintf(currency,sizeof(currency),"%s",region_currency);
            generation=region_generation;
            xSemaphoreGive(mutex);
            if(!area[0]) {   // no bidding zone chosen: nothing to fetch
                ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5000));
                continue;
            }
            auto fresh=forecast_buffer<PriceCache>();
            if(fresh) {
                fresh->fetched_utc=now;
                bool today_ok=false;
                uint8_t code=0;
                int http=0;
                for(int day=0;day<2;++day) {
                    char date[16];
                    size_t got=0;
                    if(!enevina::nordpool_market_date(now,day,date,sizeof(date))) break;
                    const bool ok=fetch_day(date,area,currency,
                        fresh->prices.data()+fresh->count,
                        max_slots-fresh->count,got,code,http);
                    if(day==0) {
                        today_ok=ok;
                        xSemaphoreTake(mutex,portMAX_DELAY);
                        if(generation==region_generation) {
                            last_error=ok?0:code;
                            last_http_status=http;
                            last_attempt_utc=now;
                        }
                        xSemaphoreGive(mutex);
                    }
                    if(!ok) break; // Tomorrow may not be published yet.
                    fresh->count+=got;
                }
                if(today_ok && valid_prices(*fresh)) {
                    xSemaphoreTake(mutex,portMAX_DELAY);
                    bool accepted=false,changed=false;
                    if(generation==region_generation) {
                        // A successful today fetch is not proof that yesterday's
                        // validated tomorrow prices have disappeared. Keep any
                        // contiguous future tail if tomorrow is unavailable.
                        preserve_future_price_tail(*cache,*fresh);
                        accepted=valid_prices(*fresh);
                        changed=accepted && !same_prices(*cache,*fresh);
                        if(accepted) {
                            *cache=*fresh;++cache_revision;
                        }
                    }
                    xSemaphoreGive(mutex);
                    // Do not erase valid future prices from NVS on a partial fetch.
                    if(accepted && changed) {
                        MarketIdentity saved_id{};
                        std::snprintf(saved_id.area,sizeof(saved_id.area),"%s",area);
                        std::snprintf(saved_id.currency,sizeof(saved_id.currency),"%s",currency);
                        saved_id.generation=generation;
                        save_cache(*fresh,saved_id);
                    }
                    updated=accepted;
                    if(accepted) ESP_LOGI(tag,"validated %lu %s price slots (%s)",
                        static_cast<unsigned long>(fresh->count),area,currency);
                    else ESP_LOGW(tag,"price refresh discarded (region changed or invalid)");
                } else ESP_LOGW(tag,"price fetch incomplete (code %u http %d); retaining validated cache",
                    static_cast<unsigned>(code),http);

            }
        }
        const int next_ms=updated ? 30*60*1000 :
            (now>1700000000 ? 60*1000 : 5*1000);
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(next_ms));
    }
}
}

uint32_t market_reader_stack_free() {
    return reader_task?uxTaskGetStackHighWaterMark(reader_task):0;
}
void start_market_reader() {
    if(reader_task)return;
    if(!mutex) mutex=xSemaphoreCreateMutex();
    if(!cache)cache=forecast_buffer<PriceCache>();
    if(!mutex||!cache) { ESP_LOGE(tag,"market state allocation failed"); return; }
    if(xTaskCreate(poll_prices,"market_read",8192,nullptr,3,&reader_task)!=pdPASS) {
        reader_task=nullptr;
        ESP_LOGE(tag,"market task creation failed");
    }
}

#ifdef COTTAGE_SHARED_MEMORY_GUARD
void market_retry_reader_start() {
    if(reader_task)return;
    NetworkBudgetLease lease(0,0,0);
    if(lease.held())start_market_reader();
}
#endif
bool market_set_region(const char* area,const char* currency) {
    // nullptr/nullptr = no zone chosen yet: prices are not fetched and the
    // cache is empty, never silently defaulted to another country's market.
    const bool disabled=!area && !currency;
    if(!disabled && (!enevina::np_detail::upper_alnum_dash(area,2,6) ||
       !enevina::np_detail::upper_alnum_dash(currency,3,3) ||
       std::strlen(area)>=sizeof(region_area))) return false;
    if(disabled) {area="";currency="";}
    if(!mutex) mutex=xSemaphoreCreateMutex();
    if(!cache)cache=forecast_buffer<PriceCache>();
    if(!mutex||!cache)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const bool changed=std::strcmp(region_area,area)!=0 ||
        std::strcmp(region_currency,currency)!=0;
    // First adoption also fences an in-flight default-region load.
    if(changed) {
        std::snprintf(region_area,sizeof(region_area),"%s",area);
        std::snprintf(region_currency,sizeof(region_currency),"%s",currency);
        ++region_generation;++cache_revision;
        // Clear only RAM metadata. The reader is the sole cache writer.
        cache->count=0;cache->fetched_utc=0;
        last_error=0;last_http_status=0;last_attempt_utc=0;
    }
    xSemaphoreGive(mutex);
    if(changed && reader_task)xTaskNotifyGive(reader_task);
    return true;
}

MarketStatus market_status() {
    MarketStatus status;
    const time_t now=time(nullptr);
    status.clock_trusted=now>1700000000;
    if(!mutex || !cache || !status.clock_trusted) return status;
    xSemaphoreTake(mutex,portMAX_DELAY);
    status.region_generation=region_generation;
    status.cache_revision=cache_revision;
    status.slots=cache->count;
    status.last_fetch_utc=cache->fetched_utc;
    std::snprintf(status.area,sizeof(status.area),"%s",region_area);
    std::snprintf(status.currency,sizeof(status.currency),"%s",region_currency);
    status.last_error=last_error;
    status.last_http_status=last_http_status;
    status.last_attempt_utc=last_attempt_utc;
    for(size_t i=0;i<cache->count;++i) {
        const auto& p=cache->prices[i];
        if(p.start_utc<=now && now<p.end_utc) {
            status.current_price_valid=true;
            status.current_spot_eur_per_kwh=p.eur_per_mwh/1000.0;
            status.valid_until_utc=cache->prices[cache->count-1].end_utc;
            break;
        }
    }
    xSemaphoreGive(mutex);
    return status;
}

size_t future_prices(PricePoint* output, size_t capacity,const MarketStatus* expected) {
    const time_t now=time(nullptr);
    if(!output || !mutex || !cache || now<=1700000000) return 0;
    size_t count=0;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(expected && (expected->region_generation!=region_generation ||
       expected->cache_revision!=cache_revision)) {
        xSemaphoreGive(mutex);return 0;
    }
    for(size_t i=0;i<cache->count && count<capacity;++i) {
        const auto& p=cache->prices[i];
        if(p.end_utc>now) output[count++]={p.start_utc,p.eur_per_mwh/1000.0};
    }
    xSemaphoreGive(mutex);
    return count;
}
}
