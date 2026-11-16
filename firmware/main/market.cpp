#include "market.hpp"
#include "market_cache.hpp"
#include "forecast/sources.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

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

void load_cache() {
    nvs_handle_t handle;
    if(nvs_open("enevina_market",NVS_READONLY,&handle)!=ESP_OK) return;
    auto* saved=new(std::nothrow) PriceCache;
    if(saved) {
        size_t size=sizeof(*saved);
        const bool ok=nvs_get_blob(handle,"price_v1",saved,&size)==ESP_OK &&
            size==sizeof(*saved) && valid_prices(*saved);
        if(ok) cache=*saved;
        delete saved;
    }
    nvs_close(handle);
}

void save_cache(const PriceCache& value) {
    nvs_handle_t handle;
    if(nvs_open("enevina_market",NVS_READWRITE,&handle)!=ESP_OK) return;
    if(nvs_set_blob(handle,"price_v1",&value,sizeof(value))==ESP_OK)
        nvs_commit(handle);
    nvs_close(handle);
}

bool fetch_day(const char* date, enevina::MarketPrice* output, size_t capacity,
    size_t& count) {
    count=0;
    char url[220];
    const int n=std::snprintf(url,sizeof(url),
        "https://dataportal-api.nordpoolgroup.com/api/DayAheadPrices?"
        "currency=EUR&market=DayAhead&deliveryArea=FI&date=%s",date);
    if(n<0 || n>=static_cast<int>(sizeof(url))) return false;
    esp_http_client_config_t cfg{};
    cfg.url=url;
    cfg.timeout_ms=10000;
    #ifdef ARDUINO
    cfg.crt_bundle_attach=arduino_esp_crt_bundle_attach;
    #else
    cfg.crt_bundle_attach=esp_crt_bundle_attach;
    #endif
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client) return false;
    bool ok=false;
    char* body=nullptr;
    if(esp_http_client_open(client,0)==ESP_OK) {
        esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client)==200) {
            constexpr size_t limit=48000;
            body=static_cast<char*>(std::malloc(limit+1));
            if(body) {
                size_t used=0;
                int read=0;
                while(used<limit && (read=esp_http_client_read(client,body+used,
                    static_cast<int>(limit-used)))>0) used+=static_cast<size_t>(read);
                if(read==0 && used>0) {
                    body[used]='\0';
                    cJSON* root=cJSON_Parse(body);
                    cJSON* entries=cJSON_GetObjectItemCaseSensitive(root,"multiAreaEntries");
                    const int length=cJSON_IsArray(entries) ? cJSON_GetArraySize(entries) : 0;
                    ok=length>0 && length<=static_cast<int>(capacity);
                    for(int i=0;ok && i<length;++i) {
                        cJSON* entry=cJSON_GetArrayItem(entries,i);
                        cJSON* start=cJSON_GetObjectItemCaseSensitive(entry,"deliveryStart");
                        cJSON* end=cJSON_GetObjectItemCaseSensitive(entry,"deliveryEnd");
                        cJSON* areas=cJSON_GetObjectItemCaseSensitive(entry,"entryPerArea");
                        cJSON* fi=cJSON_GetObjectItemCaseSensitive(areas,"FI");
                        enevina::MarketPrice value;
                        ok=cJSON_IsString(start) && cJSON_IsString(end) &&
                            cJSON_IsNumber(fi) && std::isfinite(fi->valuedouble) &&
                            enevina::parse_utc_z(start->valuestring,value.start_utc) &&
                            enevina::parse_utc_z(end->valuestring,value.end_utc);
                        if(ok) {
                            value.eur_per_mwh=fi->valuedouble;
                            output[count++]=value;
                        }
                    }
                    cJSON_Delete(root);
                }
            }
        }
        esp_http_client_close(client);
    }
    std::free(body);
    esp_http_client_cleanup(client);
    return ok;
}

bool local_date(time_t now, int offset_days, char* output, size_t size) {
    struct tm local{};
    if(!localtime_r(&now,&local)) return false;
    local.tm_mday+=offset_days;
    local.tm_hour=12;
    local.tm_isdst=-1;
    if(mktime(&local)==static_cast<time_t>(-1)) return false;
    return std::strftime(output,size,"%Y-%m-%d",&local)>0;
}

void poll_prices(void*) {
    // Finland's civil dates are needed only for the API query. Slots remain UTC.
    setenv("TZ","EET-2EEST,M3.5.0/3,M10.5.0/4",1);
    tzset();
    #ifndef ARDUINO
    esp_sntp_config_t sntp=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
    #endif
    load_cache();
    while(true) {
        const time_t now=time(nullptr);
        bool updated=false;
        if(now>1700000000) {
            auto* fresh=new(std::nothrow) PriceCache;
            if(fresh) {
                fresh->fetched_utc=now;
                bool today_ok=false;
                for(int day=0;day<2;++day) {
                    char date[16];
                    size_t got=0;
                    if(!local_date(now,day,date,sizeof(date))) break;
                    const bool ok=fetch_day(date,fresh->prices.data()+fresh->count,
                        max_slots-fresh->count,got);
                    if(day==0) today_ok=ok;
                    if(!ok) break; // Tomorrow may not be published yet.
                    fresh->count+=got;
                }
                if(today_ok && valid_prices(*fresh)) {
                    xSemaphoreTake(mutex,portMAX_DELAY);
                    // A successful today fetch is not proof that yesterday's
                    // validated tomorrow prices have disappeared. Keep any
                    // contiguous future tail if tomorrow is unavailable.
                    preserve_future_price_tail(cache,*fresh);
                    const bool accepted=valid_prices(*fresh);
                    const bool changed=accepted && !same_prices(cache,*fresh);
                    if(accepted) cache=*fresh;
                    xSemaphoreGive(mutex);
                    // Do not erase valid future prices from NVS on a partial fetch.
                    if(accepted && changed) save_cache(*fresh);
                    updated=accepted;
                    if(accepted) ESP_LOGI(tag,"validated %lu Finnish price slots",
                        static_cast<unsigned long>(fresh->count));
                    else ESP_LOGW(tag,"partial price refresh invalid; cache retained");
                } else ESP_LOGW(tag,"price fetch incomplete; retaining validated cache");
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
    mutex=xSemaphoreCreateMutex();
    if(!mutex) { ESP_LOGE(tag,"market mutex allocation failed"); return; }
    if(xTaskCreate(poll_prices,"market_read",8192,nullptr,3,nullptr)!=pdPASS)
        ESP_LOGE(tag,"market task creation failed");
}

MarketStatus market_status() {
    MarketStatus status;
    const time_t now=time(nullptr);
    status.clock_trusted=now>1700000000;
    if(!mutex || !status.clock_trusted) return status;
    xSemaphoreTake(mutex,portMAX_DELAY);
    status.slots=cache.count;
    status.last_fetch_utc=cache.fetched_utc;
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
