#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstring>
#include "cottage_shelly_discovery.hpp"

namespace enevina_firmware {
namespace {
using namespace enevina::cottage;
SemaphoreHandle_t guard=nullptr;
TaskHandle_t worker=nullptr;
ShellyScanSnapshot shared{};
uint32_t finished_ms=0;
uint32_t started_ms=0;
constexpr size_t kBodyMax=4096;

// Fetches one small LAN document. Content-Length must be present and small:
// no unbounded read of whatever answers on that address.
bool fetch(const char* url,char* buf,size_t cap) {
    HTTPClient http;
    http.setTimeout(2000);
    http.setConnectTimeout(1500);
    if(!http.begin(url)) return false;
    const int code=http.GET();
    bool ok=false;
    if(code==200) {
        const int size=http.getSize();
        if(size>0 && static_cast<size_t>(size)<cap) {
            const String body=http.getString();
            if(body.length()==static_cast<size_t>(size)) {
                std::memcpy(buf,body.c_str(),body.length()+1);
                ok=true;
            }
        }
    }
    http.end();
    return ok;
}

void inspect(const char* host,const IPAddress& ip,ShellyScanSnapshot& out) {
    if(out.count>=kShellyMaxFound) return;
    const uint8_t octets[4]={ip[0],ip[1],ip[2],ip[3]};
    ShellyFound f{};
    if(!shelly_lan_base_url(octets,f.url,sizeof(f.url))) return;
    for(size_t i=0;i<out.count;++i)
        if(!std::strcmp(out.items[i].url,f.url)) return;          // already listed (two services)
    std::snprintf(f.host,sizeof(f.host),"%s",host);
    char path[64];
    static char body[kBodyMax];   // single worker task: one shared scratch buffer
    if(!shelly_read_url(f.url,"/shelly",path,sizeof(path)))return;
    if(!fetch(path,body,sizeof(body))) return;                      // not answering like a Shelly
    if(!parse_shelly_info(body,std::strlen(body),f.info)) return;   // some other web server
    if(f.info.support==ShellySupport::Supported) {
        if(!shelly_read_url(f.url,"/rpc/Switch.GetStatus?id=0",path,sizeof(path)))return;
        double w=0;
        if(fetch(path,body,sizeof(body)) && parse_shelly_power(body,std::strlen(body),w)) {
            f.has_power=true;f.watts=w;
        }
    }
    out.items[out.count++]=f;
}

void run_scan() {
    ShellyScanSnapshot result{};
    // Gen2+ advertise _shelly._tcp; Gen1 (and some Gen2) advertise _http._tcp
    // with a shelly... host name. Both are tried; only verified Shellys are kept.
    int n=MDNS.queryService("shelly","tcp");
    for(int i=0;i<n && result.count<kShellyMaxFound;++i) {
        const String host=MDNS.hostname(i);
        inspect(host.c_str(),MDNS.IP(i),result);
    }
    n=MDNS.queryService("http","tcp");
    for(int i=0;i<n && result.count<kShellyMaxFound;++i) {
        const String host=MDNS.hostname(i);
        if(!shelly_hostname_candidate(host.c_str())) continue;
        inspect(host.c_str(),MDNS.IP(i),result);
    }
    xSemaphoreTake(guard,portMAX_DELAY);
    shared=result;
    shared.state=ShellyScanState::Done;
    finished_ms=millis();
    xSemaphoreGive(guard);
}

void worker_main(void*) {
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        run_scan();
    }
}
} // namespace

ShellyScanState shelly_scan(ShellyScanSnapshot& out) {
    if(!guard) guard=xSemaphoreCreateMutex();
    if(!guard) {out=ShellyScanSnapshot{};out.state=ShellyScanState::Failed;return out.state;}
    const uint32_t now=millis();
    bool start=false;
    xSemaphoreTake(guard,portMAX_DELAY);
    const bool pending=shared.state==ShellyScanState::Pending;
    // Results are kept for 8 s AFTER THE SCAN FINISHED (a scan itself may take
    // longer than that: sequential mDNS queries plus HTTP probes). Counting from
    // the start made a slow scan look "old" on the first poll after it completed,
    // so it was thrown away and restarted - the wizard never saw its devices.
    const uint32_t since=shared.state==ShellyScanState::Done?finished_ms:started_ms;
    const bool recent=shared.state!=ShellyScanState::Idle &&
        since && now-since<8000;
    if(!pending && !recent) {
        shared.state=ShellyScanState::Pending;
        shared.count=0;
        started_ms=now;
        start=true;
    }
    out=shared;
    out.busy_cooldown=!pending && recent && !start;
    out.age_ms=shared.state==ShellyScanState::Done?now-finished_ms:0;
    xSemaphoreGive(guard);
    if(start) {
        if(!worker && xTaskCreate(worker_main,"shelly_scan",7168,nullptr,2,&worker)!=pdPASS) {
            xSemaphoreTake(guard,portMAX_DELAY);
            shared.state=ShellyScanState::Failed;
            out=shared;
            xSemaphoreGive(guard);
            return out.state;
        }
        xTaskNotifyGive(worker);
    }
    return out.state;
}
} // namespace enevina_firmware
#endif // COTTAGE_BENCH_MODE
