#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include "cottage_geocode.hpp"
#include "cottage_network_budget.hpp"
#include "../../main/tls_roots.hpp"

namespace enevina_firmware {
namespace {
constexpr char tag[]="enevina_geo";
constexpr size_t kBodyLimit=48*1024;
constexpr uint32_t kReuseMs=120*1000;
constexpr uint32_t kMinGapMs=2000;

SemaphoreHandle_t guard=nullptr;
GeocodeSnapshot shared{};        // last/active job, guarded by `guard`
char job_url[300]{};
uint32_t finished_ms=0;
uint32_t started_ms=0;

void fail(GeocodeError e) {
    xSemaphoreTake(guard,portMAX_DELAY);
    shared.state=GeocodeState::Failed;
    shared.error=e;
    shared.count=0;
    finished_ms=millis();
    xSemaphoreGive(guard);
}

struct SearchWorkspace {
    char body[kBodyLimit];
    enevina::GeoPlace places[kGeocodeMaxResults];
};

GeocodeError download(char* body, size_t& used) {
    // mbedTLS still needs internal RAM. The response and parsed places live
    // in PSRAM, so only the bounded HTTPS exchange holds the shared gate.
    NetworkBudgetLease lease(8000,52*1024,16*1024);
    if(!lease.held()) {
        if(lease.diagnostic().failure==NetworkBudgetFailure::MutexBusy)
            return GeocodeError::Busy;
        const auto& detail=lease.diagnostic();
        ESP_LOGW(tag,"geocode TLS budget refused: free=%u largest=%u",
                 static_cast<unsigned>(detail.free_internal),
                 static_cast<unsigned>(detail.largest_internal));
        return GeocodeError::LowMemory;
    }
    esp_http_client_config_t cfg{};
    cfg.url=job_url;
    cfg.timeout_ms=10000;
    cfg.cert_pem=photon_ca_pem;
    cfg.disable_auto_redirect=true;
    cfg.buffer_size=1024;
    cfg.buffer_size_tx=512;
    auto client=esp_http_client_init(&cfg);
    if(!client)return GeocodeError::LowMemory;

    GeocodeError error=GeocodeError::Offline;
    const auto opened=esp_http_client_open(client,0);
    if(opened==ESP_OK) {
        const auto length=esp_http_client_fetch_headers(client);
        const int status=esp_http_client_get_status_code(client);
        if(status>0 && status!=200)error=GeocodeError::ServerError;
        else if(status==200 && length>static_cast<int>(kBodyLimit))
            error=GeocodeError::BadResponse;
        else if(status==200) {
            int read=0;
            while(used<kBodyLimit &&
                  (read=esp_http_client_read(client,body+used,
                                             static_cast<int>(kBodyLimit-used)))>0)
                used+=static_cast<size_t>(read);
            if(used && esp_http_client_is_complete_data_received(client))
                error=GeocodeError::None;
            else if(used==kBodyLimit)error=GeocodeError::BadResponse;
        }
        esp_http_client_close(client);
    } else {
        ESP_LOGW(tag,"geocode HTTPS open failed: %s",esp_err_to_name(opened));
        if(opened==ESP_ERR_NO_MEM)error=GeocodeError::LowMemory;
    }
    esp_http_client_cleanup(client);
    return error;
}

void run_job() {
    auto* mem=heap_caps_malloc(sizeof(SearchWorkspace),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!mem) {fail(GeocodeError::LowMemory);return;}
    std::unique_ptr<SearchWorkspace,decltype(&heap_caps_free)> data(
        new(mem) SearchWorkspace,heap_caps_free);
    size_t used=0;
    const auto error=download(data->body,used);
    if(error!=GeocodeError::None) {fail(error);return;}

    char country[3];
    xSemaphoreTake(guard,portMAX_DELAY);
    std::memcpy(country,shared.country,sizeof(country));
    xSemaphoreGive(guard);
    size_t count=0;
    if(!enevina::parse_geocode(data->body,used,country[0]?country:nullptr,
                               data->places,kGeocodeMaxResults,count)) {
        fail(GeocodeError::BadResponse);
        return;
    }
    xSemaphoreTake(guard,portMAX_DELAY);
    std::memcpy(shared.places,data->places,sizeof(shared.places));
    shared.count=count;
    shared.state=GeocodeState::Done;
    shared.error=GeocodeError::None;
    finished_ms=millis();
    xSemaphoreGive(guard);
}

void worker_main(void*) {
    run_job();
    vTaskDelete(nullptr);
}
} // namespace

GeocodeState geocode_search(const char* query,const char* country,
                            const char* language,GeocodeSnapshot& out) {
    out=GeocodeSnapshot{};
    if(!guard) guard=xSemaphoreCreateMutex();
    if(!guard) {out.state=GeocodeState::Failed;out.error=GeocodeError::LowMemory;return out.state;}
    if(!country) country="";
    if(*country && !(std::strlen(country)==2 && country[0]>='A' && country[0]<='Z' &&
                     country[1]>='A' && country[1]<='Z')) country="";
    if(!language || (std::strcmp(language,"fi") && std::strcmp(language,"en"))) language="en";
    char url[300]{};
    if(!enevina::build_geocode_url(query,language,url,sizeof(url))) {
        out.state=GeocodeState::Failed;
        out.error=GeocodeError::BadQuery;
        return out.state;
    }
    const uint32_t now=millis();
    xSemaphoreTake(guard,portMAX_DELAY);
    const bool same=!std::strcmp(shared.query,query) && !std::strcmp(shared.country,country) &&
                    !std::strcmp(shared.language,language);
    bool start=false;
    if(shared.state==GeocodeState::Pending) {
        // A different search while one runs is refused, not queued.
        if(!same) {
            xSemaphoreGive(guard);
            out.state=GeocodeState::Failed;
            out.error=GeocodeError::Busy;
            return out.state;
        }
    } else if(same && shared.state!=GeocodeState::Idle &&
              (shared.state==GeocodeState::Done ? now-finished_ms<kReuseMs
                                                : now-finished_ms<kMinGapMs*2)) {
        // Fresh result (or a very recent failure) for this exact query.
    } else if(started_ms && now-started_ms<kMinGapMs) {
        xSemaphoreGive(guard);
        out.state=GeocodeState::Failed;
        out.error=GeocodeError::Busy;
        return out.state;
    } else {
        std::strncpy(shared.query,query,sizeof(shared.query)-1);
        std::strncpy(shared.country,country,sizeof(shared.country)-1);
        std::strncpy(shared.language,language,sizeof(shared.language)-1);
        shared.state=GeocodeState::Pending;
        shared.error=GeocodeError::None;
        shared.count=0;
        std::memcpy(job_url,url,sizeof(job_url));
        started_ms=now;
        start=true;
    }
    out=shared;
    xSemaphoreGive(guard);
    if(start && xTaskCreate(worker_main,"geocode",6144,nullptr,2,nullptr)!=pdPASS) {
        fail(GeocodeError::LowMemory);
        xSemaphoreTake(guard,portMAX_DELAY);
        out=shared;
        xSemaphoreGive(guard);
    }
    return out.state;
}
} // namespace enevina_firmware
#endif // COTTAGE_BENCH_MODE
