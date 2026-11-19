#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <IPAddress.h>
#include <esp_log.h>
#include <cstring>
#include <atomic>
#include <ctime>
#include "cottage_midea_service.hpp"
#include "cottage_midea.hpp"
#include "cottage_air_service.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_wifi.hpp"
#include "cottage_network_budget.hpp"

namespace {
constexpr uint32_t config_magic=0x4d563331; // MV31
constexpr uint16_t config_version=1;
struct Config {
    uint32_t magic=config_magic;
    uint16_t version=config_version;
    uint16_t size=sizeof(Config);
    char host[16]{};
    char device_id[22]{};
    char token_hex[129]{};
    char key_hex[65]{};
};
Config saved{};
SemaphoreHandle_t mutex=nullptr;
std::atomic<uint32_t> revision{0};
std::atomic<int> last_result{0};
std::atomic<int64_t> last_success{0};
enevina::midea::State observed{};
bool configured=false;
bool is_midea_selected() {
    const auto s=site_settings_snapshot();
    return std::strcmp(s.pump_adapter,"midea_lan")==0;
}
bool private_ipv4(const char* host) {
    if(!host)return false;
    IPAddress ip;
    if(!ip.fromString(host))return false;
    const uint8_t a=ip[0],b=ip[1];
    return a==10 || (a==172 && b>=16 && b<=31) ||
           (a==192 && b==168);
}
bool valid_config(const Config& c) {
    if(c.magic!=config_magic || c.version!=config_version || c.size!=sizeof(Config) ||
       !memchr(c.host,0,sizeof(c.host)) ||
       !memchr(c.device_id,0,sizeof(c.device_id)) ||
       !memchr(c.token_hex,0,sizeof(c.token_hex)) ||
       !memchr(c.key_hex,0,sizeof(c.key_hex)) ||
       !private_ipv4(c.host))return false;
    enevina::midea::Credential credentials{};
    return enevina::midea::decode_credentials(
        c.device_id,c.token_hex,c.key_hex,credentials);
}
bool load_config(Config& dest) {
    Preferences preferences;
    if(!preferences.begin("midea_v3",true))return false;
    const bool ok=preferences.getBytesLength("config_v1")==sizeof(dest) &&
        preferences.getBytes("config_v1",&dest,sizeof(dest))==sizeof(dest) &&
        valid_config(dest);
    preferences.end();
    return ok;
}
bool persist_config(const Config& c) {
    Preferences preferences;
    if(!preferences.begin("midea_v3",false))return false;
    const bool ok=preferences.putBytes("config_v1",&c,sizeof(c))==sizeof(c);
    preferences.end();
    return ok;
}
void send_status(WebServer& web) {
    const bool selected=is_midea_selected();
    enevina::midea::State reading{};
    bool stored=false;
    int64_t last=0;
    int status=0;
    // Read the sample and its metadata in one consistent critical section.
    // Otherwise a fresh timestamp can be paired with the previous sample.
    xSemaphoreTake(mutex,portMAX_DELAY);
    reading=observed;
    stored=configured;
    last=last_success.load();
    status=last_result.load();
    xSemaphoreGive(mutex);
    const auto now=static_cast<int64_t>(time(nullptr));
    const bool fresh=selected && stored && last>0 &&
       now>=last && now-last<=90;
    char data[576];
    const int written=snprintf(data,sizeof(data),
        "{\"selected\":%s,\"configured\":%s,\"fresh\":%s,"
        "\"last_poll_utc\":%lld,\"read_status\":%d,\"power\":%s,"
        "\"mode\":%u,\"target_c\":%.1f,"
        "\"indoor_available\":%s,\"indoor_c\":%.2f,"
        "\"outdoor_available\":%s,\"outdoor_c\":%.2f}",
        selected?"true":"false",stored?"true":"false",fresh?"true":"false",
        static_cast<long long>(fresh?last:0),status,
        fresh && reading.power?"true":"false",
        fresh?static_cast<unsigned>(reading.mode):255u,
        fresh?reading.setpoint_c:0.0,
        fresh && reading.has_indoor?"true":"false",
        fresh && reading.has_indoor?reading.indoor_c:0.0,
        fresh && reading.has_outdoor?"true":"false",
        fresh && reading.has_outdoor?reading.outdoor_c:0.0);
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8",
             written>0 && written<static_cast<int>(sizeof(data))?data:"{}");
}
} // namespace

void cottage_midea_service_initialize() {
    mutex=xSemaphoreCreateMutex();
    if(!mutex)abort();
    Config candidate{};
    if(load_config(candidate)) {
        saved=candidate;
        configured=true;
    }
}
void cottage_midea_service_register_routes(WebServer& web) {
    web.on("/api/midea/status",HTTP_GET,[&web](){send_status(web);});
    web.on("/api/midea/config",HTTP_POST,[&web](){
        // This is a development-only LAN enrollment endpoint. Reject open
        // setup-AP requests; secrets must not travel on the open Wi-Fi.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
               web.arg("token").c_str())) {
            web.send(403,"application/json","{\"saved\":false,\"error\":\"TOKEN\"}");
            return;
        }
        const String host=web.arg("host");
        const String id=web.arg("device_id");
        const String tok=web.arg("v3_token");
        const String key=web.arg("v3_key");
        if(host.length()>=sizeof(Config::host) ||
           id.length()>=sizeof(Config::device_id) ||
           tok.length()>=sizeof(Config::token_hex) ||
           key.length()>=sizeof(Config::key_hex) ||
           host.isEmpty() || id.isEmpty() || tok.isEmpty() || key.isEmpty()) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"FIELDS\"}");
            return;
        }
        Config next{};
        std::memcpy(next.host,host.c_str(),host.length()+1);
        std::memcpy(next.device_id,id.c_str(),id.length()+1);
        std::memcpy(next.token_hex,tok.c_str(),tok.length()+1);
        std::memcpy(next.key_hex,key.c_str(),key.length()+1);
        if(!valid_config(next)) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"INVALID\"}");
            return;
        }
        // Never print credentials or addresses to ESP logs or API responses.
        if(!persist_config(next)) {
            web.send(507,"application/json","{\"saved\":false,\"error\":\"NVS\"}");
            return;
        }
        xSemaphoreTake(mutex,portMAX_DELAY);
        saved=next;
        configured=true;
        observed={};
        last_success.store(0);
        last_result.store(0);
        revision.fetch_add(1);
        xSemaphoreGive(mutex);
        web.send(200,"application/json","{\"saved\":true,\"control_armed\":false}");
    });
}
void cottage_midea_service_worker(void*) {
    for(;;) {
        if(WiFi.status()==WL_CONNECTED && !cottage_ota_network_maintenance() &&
           is_midea_selected()) {
            Config cfg{};
            bool ready=false;
            uint32_t before=0;
            xSemaphoreTake(mutex,portMAX_DELAY);
            cfg=saved;
            ready=configured;
            before=revision.load();
            xSemaphoreGive(mutex);
            if(ready && valid_config(cfg)) {
                enevina::midea::Credential auth{};
                if(enevina::midea::decode_credentials(
                        cfg.device_id,cfg.token_hex,cfg.key_hex,auth)) {
                    // Serialised against TLS-heavy readers; Midea uses TCP,
                    // with bounded reads, no additional watchdog/restart loop.
                    enevina_firmware::NetworkBudgetLease lease(300,60*1024,16*1024);
                    if(lease.held()) {
                        enevina::midea::State sample{};
                        const auto result=enevina::midea::read_device(cfg.host,auth,sample);
                        if(before==revision.load() && is_midea_selected()) {
                            xSemaphoreTake(mutex,portMAX_DELAY);
                            if(before==revision.load()) {
                                last_result.store(static_cast<int>(result));
                                if(result==enevina::midea::ReadStatus::Ok) {
                                    observed=sample;
                                    last_success.store(time(nullptr));
                                } else {
                                    // No live telemetry on failed authenticated read.
                                    last_success.store(0);
                                }
                            }
                            xSemaphoreGive(mutex);
                        }
                    }
                }
            }
        } else {
            // Selection/network changes invalidate any previous apparent live
            // measurement; last_result is not misrepresented as fresh.
            last_success.store(0);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
#endif