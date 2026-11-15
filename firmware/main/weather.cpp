#include "weather.hpp"
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
constexpr size_t max_points=64;
constexpr uint32_t cache_version=1;
struct WeatherCache {
    uint32_t version=cache_version;
    uint32_t count=0;
    int64_t fetched_utc=0;
    std::array<enevina::OutdoorPoint,max_points> points{};
};
WeatherCache cache;
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

bool valid_cache(const WeatherCache& data) {
    if(data.version!=cache_version || data.count<2 || data.count>max_points)
        return false;
    for(size_t i=0;i<data.count;++i) {
        const auto& p=data.points[i];
        if(p.timestamp_utc<=0 || p.timestamp_utc%3600!=0 ||
           !std::isfinite(p.temperature_c) || p.temperature_c < -60 ||
           p.temperature_c>60 ||
           (i>0 && p.timestamp_utc-data.points[i-1].timestamp_utc!=3600))
            return false;
    }
    return true;
}

bool same_points(const WeatherCache& a, const WeatherCache& b) {
    if(a.count!=b.count) return false;
    for(size_t i=0;i<a.count;++i)
        if(a.points[i].timestamp_utc!=b.points[i].timestamp_utc ||
           a.points[i].temperature_c!=b.points[i].temperature_c) return false;
    return true;
}

void load_cache() {
    nvs_handle_t handle;
    if(nvs_open("enevina_weather",NVS_READONLY,&handle)!=ESP_OK) return;
    auto* saved=new(std::nothrow) WeatherCache;
    if(saved) {
        size_t size=sizeof(*saved);
        if(nvs_get_blob(handle,"weather_v1",saved,&size)==ESP_OK &&
           size==sizeof(*saved) && valid_cache(*saved)) cache=*saved;
        delete saved;
    }
    nvs_close(handle);
}

void save_cache(const WeatherCache& data) {
    nvs_handle_t handle;
    if(nvs_open("enevina_weather",NVS_READWRITE,&handle)!=ESP_OK) return;
    if(nvs_set_blob(handle,"weather_v1",&data,sizeof(data))==ESP_OK)
        nvs_commit(handle);
    nvs_close(handle);
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
    cfg.crt_bundle_attach=esp_crt_bundle_attach;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client) return false;
    bool ok=false;
    char* body=nullptr;
    if(esp_http_client_open(client,0)==ESP_OK) {
        esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client)==200) {
            constexpr size_t limit=60000;
            body=static_cast<char*>(std::malloc(limit+1));
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
    return ok && valid_cache(next);
}

void poll_weather(void*) {
    load_cache();
    while(true) {
        const time_t now=time(nullptr);
        bool updated=false;
        if(now>1700000000) {
            auto* next=new(std::nothrow) WeatherCache;
            if(next) {
                next->fetched_utc=now;
                if(fetch_weather(*next)) {
                    xSemaphoreTake(mutex,portMAX_DELAY);
                    const bool changed=!same_points(cache,*next);
                    cache=*next;
                    xSemaphoreGive(mutex);
                    if(changed) save_cache(*next);
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
    result.points=cache.count;
    result.fetched_utc=cache.fetched_utc;
    if(cache.count>0) result.valid_until_utc=cache.points[cache.count-1].timestamp_utc;
    if(cache.fetched_utc>0 && now>=cache.fetched_utc &&
       now-cache.fetched_utc<=12*3600 &&
       result.valid_until_utc>now) {
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
