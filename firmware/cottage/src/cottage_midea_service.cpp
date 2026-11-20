#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <array>
#include <IPAddress.h>
#include <esp_log.h>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <ctime>
#include <cstdlib>
#include <cerrno>
#include <cmath>
#include "cottage_midea_service.hpp"
#include "cottage_midea.hpp"
#include "cottage_air_service.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_wifi.hpp"
#include "cottage_network_budget.hpp"
#include "pump/midea_config_migration.hpp"
#include "pump/midea_discovery.hpp"

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
std::atomic<bool> control_pending{false};
bool control_busy=false; // guarded by mutex
std::atomic<bool> config_updating{false}; // held through NVS persistence
// Per-device, explicit, volatile physical-pilot approval. Never persisted
// and never automatically inherited from read-only credentials or OTA.
struct PilotApproval {
    bool enabled=false;
    char model[48]{};
    char device_id[22]{};
    uint8_t protocol=0;
    uint32_t config_revision=0;
    double min_c=0,max_c=0;
};
PilotApproval pilot{}; // guarded by mutex
std::atomic<uint32_t> pending_wifi_generation{0};
enevina::midea::ControlPatch pending_patch{}; // guarded by mutex
std::atomic<int> control_result{-1};
std::atomic<int64_t> control_attempt{0};
std::atomic<int64_t> control_success{0};
std::atomic<int64_t> control_queued_utc{0};
enevina::midea::State observed{};
bool configured=false;
bool is_midea_selected() {
    const auto s=site_settings_snapshot();
    return std::strcmp(s.pump_adapter,"midea_lan")==0;
}
bool decimal_u8(const String& text,uint8_t& value) {
    if(text.isEmpty())return false;
    for(size_t i=0;i<text.length();++i)
        if(text[i]<'0'||text[i]>'9')return false;
    errno=0;
    char* end=nullptr;
    const unsigned long parsed=std::strtoul(text.c_str(),&end,10);
    if(errno || !end || *end || parsed>255)return false;
    value=static_cast<uint8_t>(parsed);
    return true;
}
bool strict_temperature(const String& text,double& value) {
    if(text.isEmpty())return false;
    errno=0;
    char* end=nullptr;
    const double result=std::strtod(text.c_str(),&end);
    if(errno || !end || *end || !std::isfinite(result))return false;
    value=result;
    return true;
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
bool read_legacy_v3(Preferences& preferences,Config& out) {
    LegacyConfig older{};
    const bool legacy=preferences.getBytesLength("config_v1")==sizeof(older) &&
        preferences.getBytes("config_v1",&older,sizeof(older))==sizeof(older) &&
        older.magic==legacy_magic && older.version==1 &&
        older.size==sizeof(older) &&
        memchr(older.host,0,sizeof(older.host)) &&
        memchr(older.device_id,0,sizeof(older.device_id)) &&
        memchr(older.token_hex,0,sizeof(older.token_hex)) &&
        memchr(older.key_hex,0,sizeof(older.key_hex));
    if(!legacy)return false;
    Config migrated{};
    std::memcpy(migrated.host,older.host,sizeof(older.host));
    std::memcpy(migrated.device_id,older.device_id,sizeof(older.device_id));
    std::memcpy(migrated.token_hex,older.token_hex,sizeof(older.token_hex));
    std::memcpy(migrated.key_hex,older.key_hex,sizeof(older.key_hex));
    migrated.protocol=3;
    if(!valid_config(migrated))return false;
    out=migrated;
    return true;
}
bool load_config(Config& dest) {
    Preferences preferences;
    if(!preferences.begin("midea_v3",true))return false;
    // A corrupt new key must never resurrect an intentionally retired V3
    // configuration. Only consider legacy data when v2 is either absent,
    // or valid AND explicitly set to protocol 3.
    bool ok=false;
    if(preferences.isKey("config_v2")) {
        ok=preferences.getBytesLength("config_v2")==sizeof(dest) &&
           preferences.getBytes("config_v2",&dest,sizeof(dest))==sizeof(dest) &&
           valid_config(dest);
        if(ok && dest.protocol==3) {
            Config rollback_settings{};
            const bool valid_legacy=read_legacy_v3(preferences,rollback_settings);
            // The old V3 image writes config_v1 only. After an OTA rollback,
            // later edits on that image must take precedence on re-upgrade.
            // If V2 was explicitly selected, this path is never entered.
            const enevina::midea::V3SettingsView current{
                dest.host,dest.device_id,dest.token_hex,dest.key_hex};
            const enevina::midea::V3SettingsView older{
                rollback_settings.host,rollback_settings.device_id,
                rollback_settings.token_hex,rollback_settings.key_hex};
            if(enevina::midea::rollback_v3_settings_changed(
                    dest.protocol,valid_legacy,current,older))
                dest=rollback_settings;
        }
    } else {
        ok=read_legacy_v3(preferences,dest);
    }
    preferences.end();
    return ok;
}
bool persist_config(const Config& c) {
    Preferences preferences;
    if(!preferences.begin("midea_v3",false))return false;
    if(c.protocol==3) {
        // Keep an up-to-date, rollback-readable copy for older V3 firmware
        // which knows ONLY config_v1. The OTA A/B previous image must still
        // see valid V3 credentials after an upgrade and settings save.
        LegacyConfig fallback{};
        std::memcpy(fallback.host,c.host,sizeof(fallback.host));
        std::memcpy(fallback.device_id,c.device_id,sizeof(fallback.device_id));
        std::memcpy(fallback.token_hex,c.token_hex,sizeof(fallback.token_hex));
        std::memcpy(fallback.key_hex,c.key_hex,sizeof(fallback.key_hex));
        if(preferences.putBytes("config_v1",&fallback,sizeof(fallback))
           !=sizeof(fallback)) {
            preferences.end();
            return false;
        }
    }
    // If switching to V2, retain any earlier config_v1 V3 record as the
    // previous firmware's read-only fallback. Old V3 firmware cannot read
    // a V2 config, so intentionally do not erase the rollback record.
    const bool ok=preferences.putBytes("config_v2",&c,sizeof(c))==sizeof(c);
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
        case R::ControlNotConfirmed: return "control_not_confirmed";
        case R::ResourceBusy: return "resource_busy";
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
    bool writing=false;
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
    writing=control_busy;
    xSemaphoreGive(mutex);
    const auto now=static_cast<int64_t>(time(nullptr));
    const bool fresh=selected && stored && last>0 &&
       now>=last && now-last<=90;
    const char* code=!selected?"not_selected":
        !stored?"not_configured":attempted<=0?"not_tested":read_code(status);
    const int write_status=control_result.load();
    const bool queued=control_pending.load();
    bool pilot_ok=false;
    double pilot_min=0,pilot_max=0;
    xSemaphoreTake(mutex,portMAX_DELAY);
    pilot_ok=pilot.enabled && pilot.config_revision==revision.load() &&
            pilot.protocol==saved.protocol &&
            !std::strcmp(pilot.device_id,saved.device_id) &&
            !config_updating;
    if(pilot_ok) {pilot_min=pilot.min_c;pilot_max=pilot.max_c;}
    xSemaphoreGive(mutex);
    char data[1150];
    const int written=snprintf(data,sizeof(data),
        "{\"selected\":%s,\"configured\":%s,\"fresh\":%s,"
        "\"protocol\":%u,\"read_only\":false,\"manual_control\":true,"
        "\"pilot_approved\":%s,\"pilot_min_c\":%.1f,\"pilot_max_c\":%.1f,"
        "\"control_pending\":%s,\"control_busy\":%s,"
        "\"control_last_attempt_utc\":%lld,"
        "\"control_last_confirmed_utc\":%lld,"
        "\"control_code\":\"%s\","
        "\"last_poll_utc\":%lld,\"last_attempt_utc\":%lld,"
        "\"polling\":%s,\"read_code\":\"%s\",\"read_status\":%d,\"read_detail\":%u,\"read_opcode\":%u,\"power\":%s,"
        "\"mode\":%u,\"target_c\":%.1f,"
        "\"indoor_available\":%s,\"indoor_c\":%.2f,"
        "\"outdoor_available\":%s,\"outdoor_c\":%.2f}",
        selected?"true":"false",stored?"true":"false",fresh?"true":"false",
        static_cast<unsigned>(selected_protocol),
        pilot_ok?"true":"false",pilot_min,pilot_max,
        queued?"true":"false",writing?"true":"false",
        static_cast<long long>(control_attempt.load()),
        static_cast<long long>(control_success.load()),
        write_status<0?"not_requested":read_code(write_status),
        static_cast<long long>(fresh?last:0),
        static_cast<long long>(attempted>0?attempted:0),
        busy?"true":"false",code,status,
        selected && selected_protocol==3?
            static_cast<unsigned>(enevina::midea::last_v3_read_detail()):0u,
        selected && selected_protocol==3?
            static_cast<unsigned>(enevina::midea::last_v3_reply_opcode()):0u,
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

void cottage_midea_link_disconnected() {
    // Invoked from Arduino Wi-Fi event task. No mutex/NVS or network calls.
    // Queued physical writes must never survive a Wi-Fi disconnect/reconnect.
    control_pending.store(false);
    pending_wifi_generation.store(0);
    control_result.store(static_cast<int>(
       enevina::midea::ReadStatus::ResourceBusy));
    last_success.store(0);
}
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
    web.on("/api/midea/status",HTTP_GET,[&web](){if(cottage_wifi_require_home_lan(web))send_status(web);});
    web.on("/api/midea/discover",HTTP_POST,[&web](){
        // Discovery is an explicit owner action on trusted home LAN, not the
        // open setup AP. Existing per-boot CSRF token, no new login or PIN.
        if(!cottage_wifi_require_home_lan(web))return;
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
               web.arg("token").c_str())) {
            web.send(403,"application/json","{\"error\":\"TOKEN\"}");
            return;
        }
        if(cottage_ota_network_maintenance()) {
            web.send(503,"application/json","{\"error\":\"OTA_BUSY\"}");
            return;
        }
        // One bounded UDP discovery pass. It cannot enroll a device, write
        // NVS, authenticate against a cloud, or send an HVAC command.
        enevina_firmware::NetworkBudgetLease lease(100,64*1024,16*1024);
        if(!lease.held()) {
            web.send(503,"application/json","{\"error\":\"NETWORK_BUSY\"}");
            return;
        }
        WiFiUDP udp;
        if(!udp.begin(0)) {
            web.send(503,"application/json","{\"error\":\"UDP\"}");
            return;
        }
        const auto ip=WiFi.localIP();
        const auto mask=WiFi.subnetMask();
        const IPAddress directed(
            static_cast<uint8_t>(ip[0] | ~mask[0]),
            static_cast<uint8_t>(ip[1] | ~mask[1]),
            static_cast<uint8_t>(ip[2] | ~mask[2]),
            static_cast<uint8_t>(ip[3] | ~mask[3]));
        // Both Midea discovery ports. 255.255.255.255 works on LANs which
        // reject directed broadcast. Failed sends do not create a false hit.
        const IPAddress broadcasts[2]={{255,255,255,255},directed};
        for(const auto& dest:broadcasts)
            for(const uint16_t port:{6445u,20086u}) {
                if(udp.beginPacket(dest,port)) {
                    udp.write(enevina::midea::discovery_request.data(),
                              enevina::midea::discovery_request.size());
                    udp.endPacket();
                }
            }
        struct Hit {IPAddress ip;uint64_t id;uint8_t protocol;};
        std::array<Hit,8> hits{};
        size_t count=0;
        bool truncated=false;
        const uint32_t begin=millis();
        std::array<uint8_t,512> reply{};
        while(static_cast<uint32_t>(millis()-begin)<1350u) {
            const int packet_size=udp.parsePacket();
            if(packet_size<1) {delay(15);continue;}
            const IPAddress src=udp.remoteIP();
            if(packet_size>static_cast<int>(reply.size())) {
                udp.flush();
                continue;
            }
            const int received=udp.read(reply.data(),reply.size());
            if(received!=packet_size)continue;
            // Reject responses from off-LAN hosts even if they resemble
            // Midea: this is a local setup shortlist only.
            bool on_subnet=true;
            for(uint8_t n=0;n<4;n++)
                on_subnet=on_subnet &&
                    (static_cast<uint8_t>(src[n]&mask[n])==
                     static_cast<uint8_t>(ip[n]&mask[n]));
            if(!on_subnet || src==ip)continue;
            enevina::midea::DiscoveredDevice candidate{};
            if(!enevina::midea::parse_discovery_reply(
                    reply.data(),static_cast<size_t>(received),candidate))
                continue;
            bool duplicate=false;
            for(size_t i=0;i<count;i++)
                duplicate=duplicate ||
                    (hits[i].id==candidate.id &&
                     hits[i].protocol==candidate.protocol);
            if(duplicate)continue;
            if(count==hits.size()) {truncated=true;continue;}
            hits[count++]={src,candidate.id,candidate.protocol};
        }
        udp.stop();
        // Bounded and escaped-by-construction response; no Wi-Fi credential,
        // V3 token/key, MAC address, SSID, or sensor data is returned.
        char json[1100]{};
        size_t used=0;
        auto add=[&](const char* format,auto... args)->bool{
            if(used>=sizeof(json))return false;
            const int n=std::snprintf(json+used,sizeof(json)-used,format,args...);
            if(n<0 || static_cast<size_t>(n)>=sizeof(json)-used)return false;
            used+=static_cast<size_t>(n);
            return true;
        };
        bool ok=add("{\"devices\":[");
        for(size_t i=0;i<count && ok;i++)
            ok=add("%s{\"ip\":\"%u.%u.%u.%u\",\"protocol\":%u,\"device_id\":\"%llu\"}",
                   i?",":"",hits[i].ip[0],hits[i].ip[1],
                   hits[i].ip[2],hits[i].ip[3],
                   static_cast<unsigned>(hits[i].protocol),
                   static_cast<unsigned long long>(hits[i].id));
        if(ok)ok=add("],\"count\":%u,\"truncated\":%s}",
                      static_cast<unsigned>(count),truncated?"true":"false");
        web.sendHeader("Cache-Control","no-store");
        web.send(ok?200:500,"application/json",ok?json:"{\"error\":\"BUFFER\"}");
    });
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
        // Claim exclusive config transition BEFORE NVS I/O. No worker
        // can consume pending control, nor can HTTP queue a new command,
        // while these credentials are being committed.
        xSemaphoreTake(mutex,portMAX_DELAY);
        const bool writing=control_busy || config_updating;
        if(!writing) {
            config_updating=true;
            control_pending.store(false);
            pending_wifi_generation.store(0);
            pilot={};
        }
        xSemaphoreGive(mutex);
        if(writing) {
            web.send(409,"application/json","{\"saved\":false,\"error\":\"CONTROL_BUSY\"}");
            return;
        }
        // Never print credentials or addresses to ESP logs or API responses.
        const bool committed=persist_config(next);
        xSemaphoreTake(mutex,portMAX_DELAY);
        config_updating=false;
        if(!committed) {
            xSemaphoreGive(mutex);
            web.send(507,"application/json","{\"saved\":false,\"error\":\"NVS\"}");
            return;
        }
        saved=next;
        configured=true;
        observed={};
        last_success.store(0);
        last_result.store(0);
        revision.fetch_add(1);
        last_attempt.store(0);
        control_pending.store(false);
        control_result.store(-1);
        control_queued_utc.store(0);
        xSemaphoreGive(mutex);
        if(auto task=poll_task.load())xTaskNotifyGive(task);
        web.send(200,"application/json","{\"saved\":true,\"control_armed\":false}");
    });
    web.on("/api/midea/pilot",HTTP_POST,[&web](){
        // Volatile, explicit approval of this exact configured Midea device
        // and tester-verified temperature limits. Same existing settings
        // token, no new login/PIN; never available on the open setup AP.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
              web.arg("token").c_str())) {
            web.send(403,"application/json","{\"approved\":false,\"error\":\"TOKEN\"}");
            return;
        }
        const bool approve=web.arg("approve")=="true";
        const bool revoke=web.arg("approve")=="false";
        if(!approve && !revoke) {
            web.send(400,"application/json","{\"approved\":false,\"error\":\"APPROVAL\"}");
            return;
        }
        const String model=web.arg("model");
        double min_c=0,max_c=0;
        if(approve && (model.length()<3 || model.length()>=48 ||
           !web.hasArg("device_id") ||
           !strict_temperature(web.arg("min_c"),min_c) ||
           !strict_temperature(web.arg("max_c"),max_c) ||
           min_c<12 || max_c>40 || min_c>=max_c ||
           std::fabs(min_c*2-std::round(min_c*2))>0.00001 ||
           std::fabs(max_c*2-std::round(max_c*2))>0.00001)) {
            web.send(400,"application/json","{\"approved\":false,\"error\":\"MODEL_LIMITS\"}");
            return;
        }
        xSemaphoreTake(mutex,portMAX_DELAY);
        const bool busy=control_busy || config_updating ||
                        control_pending.load();
        const bool same= configured && is_midea_selected() &&
            (!approve || web.arg("device_id")==String(saved.device_id));
        if(!busy && same) {
            pilot={};
            if(approve) {
                std::memcpy(pilot.model,model.c_str(),model.length()+1);
                std::memcpy(pilot.device_id,saved.device_id,
                            sizeof(pilot.device_id));
                pilot.protocol=saved.protocol;
                pilot.config_revision=revision.load();
                pilot.min_c=min_c;pilot.max_c=max_c;
                pilot.enabled=true;
            }
        }
        xSemaphoreGive(mutex);
        if(busy || !same) {
            web.send(409,"application/json","{\"approved\":false,\"error\":\"BUSY_OR_DEVICE\"}");
            return;
        }
        web.send(200,"application/json",
                 approve?"{\"approved\":true}":"{\"approved\":false}");
    });
    web.on("/api/midea/control",HTTP_POST,[&web](){
        // Manual control uses the existing local settings authorization.
        // Never permit commands from the intentionally open setup-AP.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
              web.arg("token").c_str())) {
            web.send(403,"application/json","{\"queued\":false,\"error\":\"TOKEN\"}");
            return;
        }
        enevina::midea::ControlPatch patch{};
        uint8_t byte=0;
        if(web.hasArg("power")) {
            const String value=web.arg("power");
            if(value!="true" && value!="false") {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"POWER\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlPower;
            patch.power=value=="true";
        }
        if(web.hasArg("mode")) {
            if(!decimal_u8(web.arg("mode"),byte)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"MODE\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlMode;
            patch.mode=byte;
        }
        if(web.hasArg("target_c")) {
            if(!strict_temperature(web.arg("target_c"),patch.setpoint_c)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"TEMPERATURE\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlTemperature;
        }
        if(web.hasArg("fan")) {
            if(!decimal_u8(web.arg("fan"),byte)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"FAN\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlFan;
            patch.fan=byte;
        }
        if(web.hasArg("swing")) {
            if(!decimal_u8(web.arg("swing"),byte)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"SWING\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlSwing;
            patch.swing=byte;
        }
        if(!enevina::midea::valid_control_patch(patch)) {
            web.send(400,"application/json","{\"queued\":false,\"error\":\"INVALID\"}");
            return;
        }
        const auto now=static_cast<int64_t>(time(nullptr));
        bool ready=false;
        // The existing service owns one pump, one worker and one queued
        // command. No HTTP callback writes directly to the heat pump.
        xSemaphoreTake(mutex,portMAX_DELAY);
        const auto previous=last_success.load();
        const bool pilot_matches=pilot.enabled &&
              pilot.config_revision==revision.load() &&
              pilot.protocol==saved.protocol &&
              !std::strcmp(pilot.device_id,saved.device_id);
        const double requested=(patch.changed&enevina::midea::ControlTemperature)?
              patch.setpoint_c:observed.setpoint_c;
        ready=configured && pilot_matches && !config_updating &&
              !control_busy && !control_pending.load() &&
              std::isfinite(requested) &&
              requested>=pilot.min_c && requested<=pilot.max_c &&
              previous>0 && now>=previous && now-previous<=90 &&
              poll_task.load()!=nullptr;
        if(ready && is_midea_selected()) {
            pending_patch=patch;
            pending_wifi_generation.store(cottage_wifi_link_generation());
            control_pending.store(true);
            control_queued_utc.store(now);
            control_result.store(-1);
        } else ready=false;
        xSemaphoreGive(mutex);
        if(!ready) {
            web.send(409,"application/json","{\"queued\":false,\"error\":\"NOT_READY\"}");
            return;
        }
        xTaskNotifyGive(poll_task.load());
        web.send(202,"application/json","{\"queued\":true}");
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
bool cottage_midea_service_needs_worker() {
    if(!is_midea_selected() || !mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const bool ready=configured;
    xSemaphoreGive(mutex);
    return ready;
}
void cottage_midea_service_worker(void*) {
    poll_task.store(xTaskGetCurrentTaskHandle());
    for(;;) {
        const bool connected=WiFi.status()==WL_CONNECTED &&
                             !cottage_ota_network_maintenance();
        const bool selected=is_midea_selected();
        Config cfg{};
        bool ready=false,action=false;
        uint32_t before=0;
        uint32_t expected_link=0;
        double min_c=0,max_c=0;
        bool approved=false;
        enevina::midea::ControlPatch patch{};
        xSemaphoreTake(mutex,portMAX_DELAY);
        cfg=saved;
        ready=configured;
        before=revision.load();
        if(control_pending.load()) {
            patch=pending_patch;
            expected_link=pending_wifi_generation.load();
            min_c=pilot.min_c;max_c=pilot.max_c;
            approved=pilot.enabled &&
                     pilot.config_revision==before &&
                     pilot.protocol==cfg.protocol &&
                     !std::strcmp(pilot.device_id,cfg.device_id);
            control_pending.store(false);
            if(approved && !config_updating &&
               expected_link==cottage_wifi_link_generation()) {
                control_busy=true;
                action=true;
            } else control_result.store(
                static_cast<int>(enevina::midea::ReadStatus::ResourceBusy));
        }
        xSemaphoreGive(mutex);
        enevina::midea::ReadStatus result=
            enevina::midea::ReadStatus::ResourceBusy;
        enevina::midea::State sample{};
        bool attempted=false;
        // A momentary Wi-Fi disconnection must NEVER lead to a delayed
        // command firing later when the connection returns.
        const auto now=static_cast<int64_t>(time(nullptr));
        const auto queued=control_queued_utc.load();
        const bool within_window=!action ||
            (queued>0 && now>=queued && now-queued<=15);
        if(connected && selected && ready && !config_updating &&
           valid_config(cfg) && within_window &&
           (!action || (approved &&
             expected_link==cottage_wifi_link_generation()))) {
            // Independent worker, shared RAM budget, no HTTP callback writes.
            enevina_firmware::NetworkBudgetLease lease(300,60*1024,16*1024);
            if(lease.held()) {
                attempted=true;
                polling.store(true);
                if(!action)last_attempt.store(time(nullptr));
                else control_attempt.store(time(nullptr));
                if(cfg.protocol==2) {
                    uint64_t id=0;
                    if(enevina::midea::decode_device_id(cfg.device_id,id)) {
                        result=action?
                            enevina::midea::write_device_v2(
                                cfg.host,id,patch,sample,
                                expected_link,min_c,max_c):
                            enevina::midea::read_device_v2(cfg.host,id,sample);
                    } else result=enevina::midea::ReadStatus::InvalidCredentials;
                } else {
                    enevina::midea::Credential auth{};
                    if(enevina::midea::decode_credentials(
                       cfg.device_id,cfg.token_hex,cfg.key_hex,auth)) {
                        result=action?
                            enevina::midea::write_device(
                                cfg.host,auth,patch,sample,
                                expected_link,min_c,max_c):
                            enevina::midea::read_device(cfg.host,auth,sample);
                    } else result=enevina::midea::ReadStatus::InvalidCredentials;
                }
                polling.store(false);
            }
        }
        xSemaphoreTake(mutex,portMAX_DELAY);
        if(action) {
            control_result.store(static_cast<int>(result));
            if(result==enevina::midea::ReadStatus::Ok &&
               before==revision.load() && is_midea_selected()) {
                control_success.store(time(nullptr));
            }
            control_busy=false;
        }
        if(connected && selected && ready && within_window &&
           before==revision.load()) {
            if(action || attempted) {
                last_result.store(static_cast<int>(result));
                if(result==enevina::midea::ReadStatus::Ok) {
                    observed=sample;
                    last_success.store(time(nullptr));
                } else last_success.store(0);
            }
        } else last_success.store(0);
        xSemaphoreGive(mutex);
        // A user action or settings change wakes the worker immediately.
        // Normal read polling remains every 30 seconds.
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(30000));
    }
}

#endif