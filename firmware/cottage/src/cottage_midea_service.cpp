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
// Keep the exact V3 legacy NVS layout readable after upgrading older Lite
// firmware. The new key uses a versioned format and explicit V2/V3 selection.
constexpr uint32_t legacy_magic=0x4d563331; // MV31
constexpr uint32_t config_magic=0x4d563332; // MV32
constexpr uint16_t config_version=2;
struct LegacyConfig {
    uint32_t magic=legacy_magic;
    uint16_t version=1;
    uint16_t size=sizeof(LegacyConfig);
    char host[16]{};
    char device_id[22]{};
    char token_hex[129]{};
    char key_hex[65]{};
};
struct Config {
    uint32_t magic=config_magic;
    uint16_t version=config_version;
    uint16_t size=sizeof(Config);
    char host[16]{};
    char device_id[22]{};
    char token_hex[129]{};
    char key_hex[65]{};
    uint8_t protocol=3; // Explicit protocol, no silent V3->V2 fallback.
    uint8_t reserved[3]{};
};
static_assert(sizeof(LegacyConfig)==240,"Legacy Midea V3 config NVS layout");
Config saved{};
SemaphoreHandle_t mutex=nullptr;
std::atomic<uint32_t> revision{0};
std::atomic<int> last_result{0};
std::atomic<int64_t> last_success{0};
std::atomic<int64_t> last_attempt{0};
std::atomic<bool> polling{false};
std::atomic<TaskHandle_t> poll_task{nullptr};
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
       !private_ipv4(c.host) ||
       (c.protocol!=2 && c.protocol!=3) ||
       c.reserved[0] || c.reserved[1] || c.reserved[2])return false;
    uint64_t id=0;
    if(!enevina::midea::decode_device_id(c.device_id,id))return false;
    if(c.protocol==2) {
        // A V2 client never reads V3 credentials; reject mixed-mode records.
        return c.token_hex[0]==0 && c.key_hex[0]==0;
    }
    enevina::midea::Credential credentials{};
    return enevina::midea::decode_credentials(
        c.device_id,c.token_hex,c.key_hex,credentials);
}
bool load_config(Config& dest) {
    Preferences preferences;
    if(!preferences.begin("midea_v3",true))return false;
    // If the new key exists but is damaged, don't fall back to a stale
    // legacy V3 config after the user intentionally switched to V2.
    bool ok=false;
    if(preferences.isKey("config_v2")) {
        ok=preferences.getBytesLength("config_v2")==sizeof(dest) &&
           preferences.getBytes("config_v2",&dest,sizeof(dest))==sizeof(dest) &&
           valid_config(dest);
    } else {
        LegacyConfig older{};
        const bool legacy=preferences.getBytesLength("config_v1")==sizeof(older) &&
            preferences.getBytes("config_v1",&older,sizeof(older))==sizeof(older) &&
            older.magic==legacy_magic && older.version==1 &&
            older.size==sizeof(older) &&
            memchr(older.host,0,sizeof(older.host)) &&
            memchr(older.device_id,0,sizeof(older.device_id)) &&
            memchr(older.token_hex,0,sizeof(older.token_hex)) &&
            memchr(older.key_hex,0,sizeof(older.key_hex));
        if(legacy) {
            Config migrated{};
            std::memcpy(migrated.host,older.host,sizeof(older.host));
            std::memcpy(migrated.device_id,older.device_id,sizeof(older.device_id));
            std::memcpy(migrated.token_hex,older.token_hex,sizeof(older.token_hex));
            std::memcpy(migrated.key_hex,older.key_hex,sizeof(older.key_hex));
            migrated.protocol=3;
            if(valid_config(migrated)) {dest=migrated;ok=true;}
        }
    }
    preferences.end();
    return ok;
}
bool persist_config(const Config& c) {
    Preferences preferences;
    if(!preferences.begin("midea_v3",false))return false;
    const bool ok=preferences.putBytes("config_v2",&c,sizeof(c))==sizeof(c);
    // Remove legacy duplicate credentials only after the new record was
    // committed. Never wipe on an NVS write failure.
    if(ok && preferences.isKey("config_v1"))preferences.remove("config_v1");
    preferences.end();
    return ok;
}
const char* read_code(int status) {
    using R=enevina::midea::ReadStatus;
    switch(static_cast<R>(status)) {
        case R::Ok: return "ok";
        case R::InvalidAddress: return "invalid_address";
        case R::InvalidCredentials: return "invalid_credentials";
        case R::ConnectionFailed: return "connection_failed";
        case R::AuthenticationFailed: return "authentication_failed";
        case R::ProtocolFailed: return "protocol_failed";
        case R::UnsupportedState: return "unsupported_state";
        case R::TimeNotReady: return "time_not_ready";
    }
    return "not_tested";
}
void send_status(WebServer& web) {
    const bool selected=is_midea_selected();
    enevina::midea::State reading{};
    bool stored=false;
    uint8_t selected_protocol=3;
    int64_t last=0;
    int status=-1;
    int64_t attempted=0;
    bool busy=false;
    // Read the sample and its metadata in one consistent critical section.
    // Otherwise a fresh timestamp can be paired with the previous sample.
    xSemaphoreTake(mutex,portMAX_DELAY);
    reading=observed;
    stored=configured;
    selected_protocol=saved.protocol;
    last=last_success.load();
    status=last_result.load();
    attempted=last_attempt.load();
    busy=polling.load();
    xSemaphoreGive(mutex);
    const auto now=static_cast<int64_t>(time(nullptr));
    const bool fresh=selected && stored && last>0 &&
       now>=last && now-last<=90;
    const char* code=!selected?"not_selected":
        !stored?"not_configured":attempted<=0?"not_tested":read_code(status);
    char data[650];
    const int written=snprintf(data,sizeof(data),
        "{\"selected\":%s,\"configured\":%s,\"fresh\":%s,"
        "\"protocol\":%u,\"read_only\":true,"
        "\"last_poll_utc\":%lld,\"last_attempt_utc\":%lld,"
        "\"polling\":%s,\"read_code\":\"%s\",\"read_status\":%d,\"power\":%s,"
        "\"mode\":%u,\"target_c\":%.1f,"
        "\"indoor_available\":%s,\"indoor_c\":%.2f,"
        "\"outdoor_available\":%s,\"outdoor_c\":%.2f}",
        selected?"true":"false",stored?"true":"false",fresh?"true":"false",
        static_cast<unsigned>(selected_protocol),
        static_cast<long long>(fresh?last:0),
        static_cast<long long>(attempted>0?attempted:0),
        busy?"true":"false",code,status,
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
        const String proto=web.arg("protocol");
        const uint8_t version=proto=="2"?2:proto=="3"?3:0;
        if(version==0) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"PROTOCOL\"}");
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
           host.isEmpty() || id.isEmpty() ||
           (version==3 && (tok.isEmpty() || key.isEmpty()))) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"FIELDS\"}");
            return;
        }
        Config next{};
        next.protocol=version;
        std::memcpy(next.host,host.c_str(),host.length()+1);
        std::memcpy(next.device_id,id.c_str(),id.length()+1);
        // V2 has no per-device V3 tokens; do not retain them in the new
        // record when changing from V3 to V2.
        if(version==3) {
            std::memcpy(next.token_hex,tok.c_str(),tok.length()+1);
            std::memcpy(next.key_hex,key.c_str(),key.length()+1);
        }
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
        last_attempt.store(0);
        xSemaphoreGive(mutex);
        if(auto task=poll_task.load())xTaskNotifyGive(task);
        web.send(200,"application/json","{\"saved\":true,\"control_armed\":false}");
    });
    web.on("/api/midea/test",HTTP_POST,[&web](){
        // A manual diagnostic only schedules the same read-only worker.
        // Never send manufacturer control commands from this endpoint.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
               web.arg("token").c_str())) {
            web.send(403,"application/json","{\"queued\":false,\"error\":\"TOKEN\"}");
            return;
        }
        bool ready=false;
        xSemaphoreTake(mutex,portMAX_DELAY);
        ready=configured;
        xSemaphoreGive(mutex);
        if(!is_midea_selected() || !ready || !poll_task.load()) {
            web.send(409,"application/json","{\"queued\":false,\"error\":\"NOT_READY\"}");
            return;
        }
        xTaskNotifyGive(poll_task.load());
        web.send(202,"application/json","{\"queued\":true}");
    });
}
void cottage_midea_service_worker(void*) {
    poll_task.store(xTaskGetCurrentTaskHandle());
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
                // Serialized against TLS-heavy readers; both V2 and V3
                // send only the AC 0x41 QUERY, never any control command.
                enevina_firmware::NetworkBudgetLease lease(300,60*1024,16*1024);
                if(lease.held()) {
                    polling.store(true);
                    last_attempt.store(time(nullptr));
                    enevina::midea::State sample{};
                    enevina::midea::ReadStatus result=
                        enevina::midea::ReadStatus::InvalidCredentials;
                    if(cfg.protocol==2) {
                        uint64_t id=0;
                        if(enevina::midea::decode_device_id(cfg.device_id,id))
                            result=enevina::midea::read_device_v2(cfg.host,id,sample);
                    } else {
                        enevina::midea::Credential auth{};
                        if(enevina::midea::decode_credentials(
                           cfg.device_id,cfg.token_hex,cfg.key_hex,auth))
                            result=enevina::midea::read_device(cfg.host,auth,sample);
                    }
                    polling.store(false);
                    if(before==revision.load() && is_midea_selected()) {
                        xSemaphoreTake(mutex,portMAX_DELAY);
                        if(before==revision.load()) {
                            last_result.store(static_cast<int>(result));
                            if(result==enevina::midea::ReadStatus::Ok) {
                                observed=sample;
                                last_success.store(time(nullptr));
                            } else last_success.store(0);
                        }
                        xSemaphoreGive(mutex);
                    }
                }
            }
        } else {
            // Invalidate under the same mutex as the status snapshot, so the
            // timestamp never contradicts an independently copied sample.
            xSemaphoreTake(mutex,portMAX_DELAY);
            last_success.store(0);
            xSemaphoreGive(mutex);
        }
        // A test request or a newly saved connection wakes the read-only
        // worker without blocking HTTP handling for the TCP handshake.
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(30000));
    }
}
#endif