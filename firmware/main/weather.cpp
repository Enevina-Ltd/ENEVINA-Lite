#ifdef ARDUINO
#include "tls_roots.hpp"
#include "esp_heap_caps.h"
#ifdef COTTAGE_SHARED_MEMORY_GUARD
#include "cottage_network_budget.hpp"
#endif
#endif
#include "weather.hpp"
#include "weather_cache.hpp"
#include "forecast/met_norway.hpp"
#include "forecast/met_http_cache.hpp"
#include "met_norway_tls.hpp"
#include "met_gzip.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
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
uint32_t location_generation=0;
TaskHandle_t weather_task=nullptr;

bool parse_location(const char* text,double& lat,double& lon) {
    if(!text || !*text) return false;
    char* next=nullptr;
    const double new_lat=std::strtod(text,&next);
    if(next==text || *next!=',') return false;
    const char* longitude_text=next+1;
    const double new_lon=std::strtod(longitude_text,&next);
    if(next==longitude_text || *next!='\0' || !std::isfinite(new_lat) ||
       !std::isfinite(new_lon) || new_lat < -90 || new_lat>90 ||
       new_lon < -180 || new_lon>180) return false;
    lat=new_lat;lon=new_lon;
    return true;
}

void load_cache(uint32_t expected_generation,
                const WeatherLocation& expected_location) {
    nvs_handle_t handle;
    if(nvs_open("enevina_weather",NVS_READONLY,&handle)!=ESP_OK) return;
    auto* saved=new(std::nothrow) WeatherCache;
    if(saved) {
        size_t size=sizeof(*saved);
        if(nvs_get_blob(handle,"weather_v2",saved,&size)==ESP_OK &&
           size==sizeof(*saved) &&
           valid_weather_cache(*saved,expected_location,time(nullptr))) {
            xSemaphoreTake(mutex,portMAX_DELAY);
            if(expected_generation==location_generation) {
                cache=*saved;
                last_saved_utc=cache.fetched_utc;
            }
            xSemaphoreGive(mutex);
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

// Response validators are captured from the signed HTTPS connection.
// Never store an expiry or Last-Modified header from another location.
struct MetResponse {
    char expires[48]{};
    char last_modified[64]{};
    char encoding[16]{};
    int status=0;
};
esp_err_t met_header(esp_http_client_event_t* event) {
    if(!event || event->event_id!=HTTP_EVENT_ON_HEADER ||
       !event->user_data || !event->header_key ||
       !event->header_value)return ESP_OK;
    auto* response=static_cast<MetResponse*>(event->user_data);
    auto copy=[&](char* dst,size_t capacity) {
        if(std::strlen(event->header_value)<capacity)
            std::strcpy(dst,event->header_value);
    };
    if(strcasecmp(event->header_key,"Expires")==0)
        copy(response->expires,sizeof(response->expires));
    else if(strcasecmp(event->header_key,"Last-Modified")==0)
        copy(response->last_modified,sizeof(response->last_modified));
    else if(strcasecmp(event->header_key,"Content-Encoding")==0)
        copy(response->encoding,sizeof(response->encoding));
    return ESP_OK;
}
bool fetch_weather(WeatherCache& next,double fetch_lat,double fetch_lon,
                   const WeatherLocation& expected_location,
                   const WeatherCache* previous,int& http_status) {
#ifdef COTTAGE_SHARED_MEMORY_GUARD
    // Do not overlap MET Norway TLS with BLE or Nord Pool. No internal-RAM fallback.
    NetworkBudgetLease network_lease(8000,78*1024,16*1024);
    if(!network_lease.held())return false;
#endif
    // MET Norway Locationforecast is global. Max 4 decimal coordinate digits
    // are REQUIRED for server-side caching (and to avoid HTTP 403).
    char url[220];
    const int n=std::snprintf(url,sizeof(url),
        "https://api.met.no/weatherapi/locationforecast/2.0/compact?"
        "lat=%.4f&lon=%.4f",fetch_lat,fetch_lon);
    if(n<0||n>=static_cast<int>(sizeof(url)))return false;
    MetResponse headers{};
    esp_http_client_config_t cfg{};
    cfg.url=url;
    cfg.timeout_ms=15000;
    cfg.event_handler=met_header;
    cfg.user_data=&headers;
#ifdef ARDUINO
    cfg.cert_pem=enevina_firmware::met_norway_ca_pem;
#else
    cfg.crt_bundle_attach=esp_crt_bundle_attach;
#endif
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client)return false;
    // Real site identity, not a generic/fake user agent. No browser requests.
    esp_http_client_set_header(client,"User-Agent",
                              "EnevinaLite/0.1 (https://enevina.com)");
    esp_http_client_set_header(client,"Accept",
                              "application/json");
    esp_http_client_set_header(client,"Accept-Encoding",
                              "gzip, deflate");
    if(previous && previous->last_modified[0])
        esp_http_client_set_header(client,"If-Modified-Since",
                                   previous->last_modified);
    bool ok=false;
    if(esp_http_client_open(client,0)==ESP_OK) {
        const int64_t content_size=esp_http_client_fetch_headers(client);
        http_status=esp_http_client_get_status_code(client);
        const int64_t now=time(nullptr);
        const int64_t http_expires=enevina::met::expires_utc(headers.expires);
        if(http_status==304 && previous &&
           valid_weather_cache(*previous,expected_location,now)) {
            // A 304 extends freshness of an already validated and
            // location-matched cache, without any unverified new values.
            next=*previous;
            next.fetched_utc=now;
            next.expires_utc=enevina::met::min_safe_retry(now,http_expires);
            if(headers.last_modified[0])
                std::strncpy(next.last_modified,headers.last_modified,
                             sizeof(next.last_modified)-1);
            next.checksum=weather_checksum(next);
            ok=valid_weather_cache(next,expected_location,now);
        } else if(http_status==200) {
            constexpr size_t input_capacity=96*1024;
            constexpr size_t output_capacity=96*1024;
#ifdef ARDUINO
            auto* body=static_cast<uint8_t*>(heap_caps_malloc(
                input_capacity+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
            auto* body=static_cast<uint8_t*>(std::malloc(input_capacity+1));
#endif
            if(body && (content_size<0 ||
                        content_size<=static_cast<int64_t>(input_capacity))) {
                size_t used=0;int received=0;
                while(used<input_capacity &&
                    (received=esp_http_client_read(client,
                         reinterpret_cast<char*>(body)+used,
                         static_cast<int>(input_capacity-used)))>0)
                    used+=static_cast<size_t>(received);
#ifdef COTTAGE_SHARED_MEMORY_GUARD
                network_budget_sample();
#endif
                if(received==0 && used>0) {
#ifdef ARDUINO
                    char* expanded=static_cast<char*>(heap_caps_malloc(
                        output_capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
                    char* expanded=static_cast<char*>(std::malloc(output_capacity));
#endif
                    size_t decoded=0,count=0;
                    if(expanded && met_expand_http(
                         headers.encoding,body,used,expanded,output_capacity,
                         decoded) && decoded>0) {
                        next=WeatherCache{};
                        next.fetched_utc=now;
                        next.expires_utc=enevina::met::min_safe_retry(
                            now,http_expires);
                        next.location=expected_location;
                        if(headers.last_modified[0])
                            std::strncpy(next.last_modified,
                              headers.last_modified,sizeof(next.last_modified)-1);
                        ok=enevina::parse_met_norway_temperature(
                            expanded,next.points.data(),next.points.size(),
                            count);
                        next.count=static_cast<uint32_t>(count);
                        next.checksum=weather_checksum(next);
                        ok=ok&&valid_weather_cache(next,expected_location,now);
                    }
                    std::free(expanded);
                }
            }
            std::free(body);
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return ok;
}

void poll_weather(void*) {
    bool cache_load_attempted=false;
    uint32_t last_generation=UINT32_MAX;
    int64_t failed_retry_not_before=0;
    while(true) {
        const time_t now=time(nullptr);
        bool updated=false;
        double selected_lat=0,selected_lon=0;
        uint32_t selected_generation=0;
        WeatherLocation selected_location{};
        bool enabled=false;
        xSemaphoreTake(mutex,portMAX_DELAY);
        selected_lat=latitude;selected_lon=longitude;
        selected_location=current_location;
        selected_generation=location_generation;
        enabled=configured;
        xSemaphoreGive(mutex);
        if(selected_generation!=last_generation) {
            last_generation=selected_generation;
            failed_retry_not_before=0;
            cache_load_attempted=false;
        }
        if(now>1700000000 && enabled) {
            // Cold boot and NTP: cache never crosses a changed location.
            if(should_load_weather_cache(cache_load_attempted,now)) {
                load_cache(selected_generation,selected_location);
                cache_load_attempted=true;
            }
            WeatherCache previous{};
            xSemaphoreTake(mutex,portMAX_DELAY);
            if(valid_weather_cache(cache,selected_location,now))
                previous=cache;
            xSemaphoreGive(mutex);
            const bool cache_present=previous.count>=2;
            const bool can_fetch=now>=failed_retry_not_before &&
                (!cache_present || now>=previous.expires_utc);
            auto* next=can_fetch ? new(std::nothrow) WeatherCache:nullptr;
            if(next) {
                int http_status=0;
                if(fetch_weather(*next,selected_lat,selected_lon,
                      selected_location,cache_present?&previous:nullptr,
                      http_status)) {
                    xSemaphoreTake(mutex,portMAX_DELAY);
                    const bool still_current=
                        selected_generation==location_generation && configured;
                    bool must_save=false;
                    if(still_current) {
                        const bool changed=!same_weather_points(cache,*next);
                        cache=*next;
                        must_save=changed || last_saved_utc<=0 ||
                                  now-last_saved_utc>=6*3600;
                        updated=true;
                    }
                    xSemaphoreGive(mutex);
                    if(still_current) {
                        if(must_save && save_cache(*next)) {
                            xSemaphoreTake(mutex,portMAX_DELAY);
                            if(selected_generation==location_generation)
                                last_saved_utc=now;
                            xSemaphoreGive(mutex);
                        }
                        ESP_LOGI(tag,"validated %lu hourly forecast points",
                            static_cast<unsigned long>(next->count));
                    }
                } else {
                    // Do not hammer MET Norway when the API is unavailable,
                    // the response is rejected, or we are rate-limited.
                    failed_retry_not_before=now+
                        (http_status==429?2*3600:3600);
                    ESP_LOGW(tag,"MET Norway unavailable (HTTP %d); cache retained",
                             http_status);
                }
                delete next;
            }
        }
        const int wait_ms=updated?60*60*1000:
                          (now>1700000000?60*1000:5*1000);
        // A new place wakes the same worker instantly; no reboot or polling
        // gap of up to 60 minutes. Notification does not start a second task.
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(wait_ms));
    }
}
}

uint32_t weather_reader_stack_free() {
    return weather_task?uxTaskGetStackHighWaterMark(weather_task):0;
}
bool ensure_weather_reader() {
    if(weather_task)return true;
    // Lite N16R8 has a shared TLS memory budget: physical MET Norway
    // polling left 3552 bytes of its 7168-byte stack unused. Retain
    // 6144 bytes under the Lite guard (still >2 KiB measured margin).
    // Non-Lite firmware keeps its original weather reader stack.
#ifdef COTTAGE_SHARED_MEMORY_GUARD
    constexpr uint32_t kWeatherReaderStack=6144;
#else
    constexpr uint32_t kWeatherReaderStack=7168;
#endif
    if(xTaskCreate(poll_weather,"weather_read",kWeatherReaderStack,nullptr,3,
                   &weather_task)==pdPASS)return true;
    ESP_LOGE(tag,"weather task creation failed");
    return false;
}

void start_weather_reader(const char* configured_latlon) {
    const char* initial=configured_latlon?
        configured_latlon:CONFIG_ENEVINA_FMI_LATLON;
    configured=parse_location(initial,latitude,longitude);
    if(configured)current_location=normalized_location(latitude,longitude);
    mutex=xSemaphoreCreateMutex();
    if(!mutex) { ESP_LOGE(tag,"weather mutex allocation failed"); return; }
    // Preserve the public setup workflow without paying for an idle 8 KiB
    // TLS worker on devices whose owner has not selected a weather location.
    // weather_update_location() starts it when the site is configured.
    if(configured)ensure_weather_reader();
}
bool weather_update_location(const char* selected) {
    const bool enabled=selected && *selected;
    double new_lat=0,new_lon=0;
    if(enabled && !parse_location(selected,new_lat,new_lon))return false;
    if(!mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const bool changed=configured!=enabled ||
       (enabled && (std::fabs(new_lat-latitude)>1e-7 ||
                    std::fabs(new_lon-longitude)>1e-7));
    if(changed) {
        latitude=new_lat;longitude=new_lon;
        current_location=enabled?
            normalized_location(new_lat,new_lon):WeatherLocation{};
        configured=enabled;
        ++location_generation;
        cache=WeatherCache{}; // Wrong-location forecast must never be shown.
        last_saved_utc=0;
    }
    xSemaphoreGive(mutex);
    // Settings already persist before this function is called; retry on any
    // subsequent location update if allocation temporarily fails.
    if(enabled && !ensure_weather_reader())return false;
    if(changed && weather_task)xTaskNotifyGive(weather_task);
    return true;
}

WeatherStatus weather_status() {
    WeatherStatus result;
    const time_t now=time(nullptr);
    if(!mutex || now<=1700000000) return result;
    xSemaphoreTake(mutex,portMAX_DELAY);
    result.configured=configured;
    if(configured && valid_weather_cache(cache,current_location,now)) {
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
    if(configured && valid_weather_cache(cache,current_location,now) &&
       cache.fetched_utc>0 && now>=cache.fetched_utc &&
       now-cache.fetched_utc<=6*3600)
        for(size_t i=0;i<cache.count && n<capacity;++i)
            output[n++]=cache.points[i];
    xSemaphoreGive(mutex);
    return n;
}
}
