#ifdef ARDUINO
#include "tls_roots.hpp"
#include "esp_heap_caps.h"
#endif
#include "weather.hpp"
#include "weather_cache.hpp"
#include "forecast/fmi.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace enevina_firmware {
namespace {
constexpr char tag[]="enevina_weather";
WeatherCache cache;
WeatherLocation current_location{};
int64_t last_saved_utc=0;
SemaphoreHandle_t mutex=nullptr;
bool configured=false;
double latitude=0,longitude=0;

bool parse_location(const char* text) {
    if(!text || !*text) return false;
    char* next=nullptr;
    latitude=std::strtod(text,&next);
    if(next==text || *next!=',') return false;
    const char* lon=next+1;
    longitude=std::strtod(lon,&next);
    return next!=lon && *next=='\0' && std::isfinite(latitude) &&
        std::isfinite(longitude) && latitude>=59 && latitude<=71 &&
        longitude>=19 && longitude<=32;
}

void load_cache() {
    nvs_handle_t handle;
    if(nvs_open("enevina_weather",NVS_READONLY,&handle)!=ESP_OK) return;
    auto* saved=new(std::nothrow) WeatherCache;
    if(saved) {
        size_t size=sizeof(*saved);
        if(nvs_get_blob(handle,"weather_v2",saved,&size)==ESP_OK &&
           size==sizeof(*saved) &&
           valid_weather_cache(*saved,current_location,time(nullptr))) {
            cache=*saved;
            last_saved_utc=cache.fetched_utc;
        }
        delete saved;
    }
    nvs_close(handle);
}

bool save_cache(const WeatherCache& data) {
    nvs_handle_t handle;
    if(nvs_open("enevina_weather",NVS_READWRITE,&handle)!=ESP_OK) return false;
    const bool saved=nvs_set_blob(handle,"weather_v2",&data,sizeof(data))==ESP_OK &&
        nvs_commit(handle)==ESP_OK;
    nvs_close(handle);
    return saved;
}

bool fetch_weather(WeatherCache& next) {
    char url[320];
    const int n=std::snprintf(url,sizeof(url),
        "https://opendata.fmi.fi/wfs?service=WFS&version=2.0.0&"
        "request=getFeature&storedquery_id=fmi::forecast::harmonie::"
        "surface::point::simple&latlon=%.5f,%.5f&parameters=temperature&timestep=60",
        latitude,longitude);
    if(n<0 || n>=static_cast<int>(sizeof(url))) return false;
    esp_http_client_config_t cfg{};
    cfg.url=url;
    cfg.timeout_ms=12000;
    #ifdef ARDUINO
    cfg.cert_pem=enevina_firmware::fmi_ca_pem;
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
            constexpr size_t limit=60000;
            #ifdef ARDUINO
            // Share scarce internal RAM with C2.3 BLE. Large downloads
            // use external PSRAM; fail closed if unavailable.
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
                if(read==0 && used>0) {
                    body[used]='\0';
                    size_t count=0;
                    ok=enevina::parse_fmi_temperature(body,next.points.data(),
                        next.points.size(),count);
                    next.count=static_cast<uint32_t>(count);
                }
            }
        }
        esp_http_client_close(client);
    }
    std::free(body);
    esp_http_client_cleanup(client);
    if(ok) next.checksum=weather_checksum(next);
    return ok && valid_weather_cache(next,current_location,time(nullptr));
}

void poll_weather(void*) {
    bool cache_load_attempted=false;
    while(true) {
        const time_t now=time(nullptr);
        bool updated=false;
        if(now>1700000000) {
            // A cold boot may have no trusted UTC yet. Do not reject the
            // persisted cache until the clock has synchronized.
            if(should_load_weather_cache(cache_load_attempted,now)) {
                load_cache();
                cache_load_attempted=true;
            }
            auto* next=new(std::nothrow) WeatherCache;
            if(next) {
                next->fetched_utc=now;
                next->location=current_location;
                if(fetch_weather(*next)) {
                    xSemaphoreTake(mutex,portMAX_DELAY);
                    const bool changed=!same_weather_points(cache,*next);
                    cache=*next;
                    xSemaphoreGive(mutex);
                    // Persist a refreshed age at most every six hours if
                    // the forecast points have not changed.
                    if((changed || last_saved_utc<=0 ||
                        now-last_saved_utc>=6*3600) && save_cache(*next))
                        last_saved_utc=now;
                    updated=true;
                    ESP_LOGI(tag,"validated %lu hourly forecast points",
                        static_cast<unsigned long>(next->count));
                } else ESP_LOGW(tag,"FMI forecast unavailable; retaining validated cache");
                delete next;
            }
        }
        const int next_ms=updated ? 60*60*1000 :
            (now>1700000000 ? 60*1000 : 5*1000);
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}
}

void start_weather_reader() {
    configured=parse_location(CONFIG_ENEVINA_FMI_LATLON);
    if(!configured) {
        ESP_LOGW(tag,"FMI location missing or invalid; weather reader disabled");
        return;
    }
    current_location=normalized_location(latitude,longitude);
    mutex=xSemaphoreCreateMutex();
    if(!mutex) { ESP_LOGE(tag,"weather mutex allocation failed"); return; }
    if(xTaskCreate(poll_weather,"weather_read",8192,nullptr,3,nullptr)!=pdPASS)
        ESP_LOGE(tag,"weather task creation failed");
}

WeatherStatus weather_status() {
    WeatherStatus result;
    result.configured=configured;
    const time_t now=time(nullptr);
    if(!mutex || now<=1700000000) return result;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(valid_weather_cache(cache,current_location,now)) {
        result.points=cache.count;
        result.fetched_utc=cache.fetched_utc;
        result.valid_until_utc=cache.points[cache.count-1].timestamp_utc;
        for(size_t i=0;i<cache.count;++i) {
            if(cache.points[i].timestamp_utc>=now) {
                result.available=true;
                result.next_utc=cache.points[i].timestamp_utc;
                result.next_temperature_c=cache.points[i].temperature_c;
                break;
            }
        }
    }
    xSemaphoreGive(mutex);
    return result;
}
}

namespace enevina_firmware {
size_t future_weather(enevina::OutdoorPoint* output,size_t capacity) {
    if(!output || !mutex || !capacity) return 0;
    const time_t now=time(nullptr);
    if(now<=1700000000) return 0;
    size_t n=0;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(cache.fetched_utc>0 && now>=cache.fetched_utc && now-cache.fetched_utc<=6*3600)
        for(size_t i=0;i<cache.count && n<capacity;++i) output[n++]=cache.points[i];
    xSemaphoreGive(mutex);
    return n;
}
}
