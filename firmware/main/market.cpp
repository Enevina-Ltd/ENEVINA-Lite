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
PriceCache cache;
SemaphoreHandle_t mutex=nullptr;
// Region the cache belongs to. Default keeps Finland/EUR so an unconfigured
// or upgraded device behaves exactly as before. Guarded by `mutex`.
char region_area[8]="FI";
char region_currency[4]="EUR";
char cache_area[8]="FI";      // region the RAM cache was fetched for
char cache_currency[4]="EUR";
uint32_t region_generation=0;
uint8_t last_error=0;          // enevina::DayAheadError of the last attempt
int last_http_status=0;
int64_t last_attempt_utc=0;
constexpr char region_key[]="price_region";

bool valid_prices(const PriceCache& candidate) {
    if(candidate.version!=cache_version || candidate.count==0 || candidate.count>max_slots)
        return false;
    auto* normalized=new(std::nothrow) enevina::PriceSlot[max_slots];
    if(!normalized) return false;
    const enevina::Tariff raw_spot{};
    const bool ok=enevina::normalize_nordpool_fi(candidate.prices.data(),candidate.count,
        raw_spot,normalized,max_slots);
    delete[] normalized;
    return ok;
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

void save_cache(const PriceCache& value,const char* stamp);
void load_cache() {
    nvs_handle_t handle;
    if(nvs_open("enevina_market",NVS_READONLY,&handle)!=ESP_OK) return;
    // A cache without a stamp predates regional pricing and is Finland/EUR.
    char stamp[16]="FI/EUR";
    size_t stamp_size=sizeof(stamp);
    if(nvs_get_str(handle,region_key,stamp,&stamp_size)!=ESP_OK)
        std::snprintf(stamp,sizeof(stamp),"FI/EUR");
    char wanted[16];
    xSemaphoreTake(mutex,portMAX_DELAY);
    std::snprintf(wanted,sizeof(wanted),"%s/%s",region_area,region_currency);
    xSemaphoreGive(mutex);
    if(!region_area[0] || std::strcmp(stamp,wanted)!=0) {
        // Prices of another bidding zone or currency must never be reused.
        nvs_close(handle);
        return;
    }
    auto* saved=new(std::nothrow) PriceCache;
    bool migrate_legacy=false;
    if(saved) {
        auto compact=std::unique_ptr<CompactPriceCache>(
            new(std::nothrow) CompactPriceCache);
        size_t size=sizeof(CompactPriceCache);
        bool ok=compact &&
            nvs_get_blob(handle,"price_v2",compact.get(),&size)==ESP_OK &&
            size==sizeof(*compact) &&
            unpack_price_cache(*compact,*saved) && valid_prices(*saved);
        if(!ok) {
            size=sizeof(*saved);
            ok=nvs_get_blob(handle,"price_v1",saved,&size)==ESP_OK &&
                size==sizeof(*saved) && valid_prices(*saved);
            migrate_legacy=ok;
        }
        if(ok) {
            xSemaphoreTake(mutex,portMAX_DELAY);
            cache=*saved;
            std::snprintf(cache_area,sizeof(cache_area),"%s",region_area);
            std::snprintf(cache_currency,sizeof(cache_currency),"%s",region_currency);
            xSemaphoreGive(mutex);
        }
        delete saved;
    }
    nvs_close(handle);
    // Move the verified in-memory legacy price cache without discarding other
    // NVS namespaces (Wi-Fi, OTA, sensors and user settings).
    if(migrate_legacy) save_cache(cache,"FI/EUR");
}

void save_cache(const PriceCache& value,const char* stamp) {
    // Market reader has a small RTOS stack. The 1.6-KiB packed buffer must
    // live on heap, not the stack (physical ESP32 stack-canary regression).
    auto compact=std::unique_ptr<CompactPriceCache>(
        new(std::nothrow) CompactPriceCache);
    if(!compact || !pack_price_cache(value,*compact)) {
        ESP_LOGW(tag,"Skipping noncontiguous price cache NVS write");
        return;
    }
    nvs_handle_t handle;
    if(nvs_open("enevina_market",NVS_READWRITE,&handle)!=ESP_OK) return;
    // The old 200 x MarketPrice blob occupies most of the 20-KiB NVS area.
    // Erase only THIS replaceable, already-validated price-cache key before
    // creating compact v2. Price RAM remains available if flash is full.
    const esp_err_t previous=nvs_erase_key(handle,"price_v1");
    if(previous!=ESP_ERR_NVS_NOT_FOUND && previous!=ESP_OK) {
        ESP_LOGW(tag,"NVS old-price removal failed: %d",previous);
        nvs_close(handle);
        return;
    }
    if(previous==ESP_OK && nvs_commit(handle)!=ESP_OK) {
        ESP_LOGW(tag,"NVS old-price deletion commit failed");
        nvs_close(handle);
        return;
    }
    if(nvs_set_blob(handle,"price_v2",compact.get(),sizeof(*compact))==ESP_OK) {
        // Region stamp AFTER the blob: a power loss between the two leaves a
        // blob whose stamp is missing/old, which load_cache() rejects unless
        // it is the legacy Finland/EUR cache.
        if(nvs_commit(handle)!=ESP_OK ||
           nvs_set_str(handle,region_key,stamp)!=ESP_OK ||
           nvs_commit(handle)!=ESP_OK)
            ESP_LOGW(tag,"NVS compact-price commit failed");
    } else {
        ESP_LOGW(tag,"NVS compact-price write failed");
    }
    nvs_close(handle);
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
            char area[8],currency[4],stamp[16];
            uint32_t generation=0;
            xSemaphoreTake(mutex,portMAX_DELAY);
            std::snprintf(area,sizeof(area),"%s",region_area);
            std::snprintf(currency,sizeof(currency),"%s",region_currency);
            generation=region_generation;
            xSemaphoreGive(mutex);
            if(!area[0]) {   // no bidding zone chosen: nothing to fetch
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            std::snprintf(stamp,sizeof(stamp),"%s/%s",area,currency);
            auto* fresh=new(std::nothrow) PriceCache;
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
                        last_error=ok?0:code;
                        last_http_status=http;
                        last_attempt_utc=now;
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
                        preserve_future_price_tail(cache,*fresh);
                        accepted=valid_prices(*fresh);
                        changed=accepted && !same_prices(cache,*fresh);
                        if(accepted) {
                            cache=*fresh;
                            std::snprintf(cache_area,sizeof(cache_area),"%s",area);
                            std::snprintf(cache_currency,sizeof(cache_currency),"%s",currency);
                        }
                    }
                    xSemaphoreGive(mutex);
                    // Do not erase valid future prices from NVS on a partial fetch.
                    if(accepted && changed) save_cache(*fresh,stamp);
                    updated=accepted;
                    if(accepted) ESP_LOGI(tag,"validated %lu %s price slots (%s)",
                        static_cast<unsigned long>(fresh->count),area,currency);
                    else ESP_LOGW(tag,"price refresh discarded (region changed or invalid)");
                } else ESP_LOGW(tag,"price fetch incomplete (code %u http %d); retaining validated cache",
                    static_cast<unsigned>(code),http);
                delete fresh;
            }
        }
        const int next_ms=updated ? 30*60*1000 :
            (now>1700000000 ? 60*1000 : 5*1000);
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}
}

void start_market_reader() {
    if(!mutex) mutex=xSemaphoreCreateMutex();
    if(!mutex) { ESP_LOGE(tag,"market mutex allocation failed"); return; }
    if(xTaskCreate(poll_prices,"market_read",8192,nullptr,3,nullptr)!=pdPASS)
        ESP_LOGE(tag,"market task creation failed");
}

bool market_set_region(const char* area,const char* currency) {
    // nullptr/nullptr = no zone chosen yet: prices are not fetched and the
    // cache is empty, never silently defaulted to another country's market.
    const bool disabled=!area && !currency;
    if(!disabled && (!enevina::np_detail::upper_alnum_dash(area,2,6) ||
       !enevina::np_detail::upper_alnum_dash(currency,3,3) ||
       std::strlen(area)>=sizeof(region_area))) return false;
    if(disabled) {area="";currency="";}
    if(!mutex) mutex=xSemaphoreCreateMutex();
    if(!mutex) return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const bool changed=std::strcmp(region_area,area)!=0 ||
        std::strcmp(region_currency,currency)!=0;
    if(changed) {
        std::snprintf(region_area,sizeof(region_area),"%s",area);
        std::snprintf(region_currency,sizeof(region_currency),"%s",currency);
        ++region_generation;
        // Prices of the previous zone/currency are never shown or reused.
        cache=PriceCache{};
        last_error=0;
        last_http_status=0;
        last_attempt_utc=0;
    }
    xSemaphoreGive(mutex);
    if(changed) {
        // Drop only the replaceable price cache and its stamp; never touch
        // other NVS namespaces or keys (Wi-Fi, OTA, sensors, settings).
        nvs_handle_t handle;
        if(nvs_open("enevina_market",NVS_READWRITE,&handle)==ESP_OK) {
            nvs_erase_key(handle,"price_v2");
            nvs_erase_key(handle,"price_v1");
            nvs_erase_key(handle,region_key);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }
    return true;
}

MarketStatus market_status() {
    MarketStatus status;
    const time_t now=time(nullptr);
    status.clock_trusted=now>1700000000;
    if(!mutex || !status.clock_trusted) return status;
    xSemaphoreTake(mutex,portMAX_DELAY);
    status.slots=cache.count;
    status.last_fetch_utc=cache.fetched_utc;
    std::snprintf(status.area,sizeof(status.area),"%s",region_area);
    std::snprintf(status.currency,sizeof(status.currency),"%s",region_currency);
    status.last_error=last_error;
    status.last_http_status=last_http_status;
    status.last_attempt_utc=last_attempt_utc;
    for(size_t i=0;i<cache.count;++i) {
        const auto& p=cache.prices[i];
        if(p.start_utc<=now && now<p.end_utc) {
            status.current_price_valid=true;
            status.current_spot_eur_per_kwh=p.eur_per_mwh/1000.0;
            status.valid_until_utc=cache.prices[cache.count-1].end_utc;
            break;
        }
    }
    xSemaphoreGive(mutex);
    return status;
}

size_t future_prices(PricePoint* output, size_t capacity) {
    const time_t now=time(nullptr);
    if(!output || !mutex || now<=1700000000) return 0;
    size_t count=0;
    xSemaphoreTake(mutex,portMAX_DELAY);
    for(size_t i=0;i<cache.count && count<capacity;++i) {
        const auto& p=cache.prices[i];
        if(p.end_utc>now) output[count++]={p.start_utc,p.eur_per_mwh/1000.0};
    }
    xSemaphoreGive(mutex);
    return count;
}
}
