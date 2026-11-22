#ifndef COTTAGE_BENCH_MODE
#include "cottage_daikin_dsiot_service.hpp"
#include "cottage_air_service.hpp"
#include "cottage_network_budget.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "pump/daikin_dsiot.hpp"
#include "cottage_local_http.hpp"

#include <HTTPClient.h>
#include <algorithm>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cJSON.h>
#include <cstring>
#include <ctime>

namespace {
using namespace enevina::cottage;
using namespace enevina::dsiot;
constexpr char adapter_id[]="daikin_dsiot";
constexpr uint32_t config_schema=1,read_interval_ms=30000,freshness_ms=90000;

struct Config {
    uint32_t schema=0;
    char host[16]{},mac[13]{};
};
struct Link {
    uint32_t endpoint=0,wifi=0;
    bool current() const {
        return WiFi.status()==WL_CONNECTED &&
            endpoint==site_settings_endpoint_generation() && wifi==cottage_wifi_link_generation();
    }
};
struct Snapshot {
    Config config{};
    Reading reading{};
    Link link{};
    uint32_t received_ms=0;
    int64_t received_utc=0;
    bool received=false;
};
enum class Task { None, Identify, Read };
struct Work {
    Task task=Task::None;
    Link link{};
    char host[16]{};
    bool draft=false;
};
struct State {
    Config saved{};
    Snapshot active{},staged{};
    uint32_t revision=0,stage_id=0,next_id=0;
    Work pending{};
    bool busy=false;
    const char* status="not_tested";
    const char* draft_status="not_tested";
};
SemaphoreHandle_t gate=nullptr;
State state{};

Link live_link() {
    return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};
}
bool configured(const Config& config) {
    if(config.schema!=config_schema || !std::memchr(config.host,0,sizeof(config.host)) ||
       !std::memchr(config.mac,0,sizeof(config.mac)) || std::strlen(config.mac)!=12)return false;
    IPAddress address;
    return address.fromString(config.host);
}
bool persists(const Config& c) {
    Preferences p;
    if(!p.begin("cottage_dso",false) || p.putBytes("link1",&c,sizeof(c))!=sizeof(c))return false;
    Config stored{};
    return p.getBytes("link1",&stored,sizeof(stored))==sizeof(stored) &&
        stored.schema==c.schema && !std::strcmp(c.host,stored.host) &&
        !std::strcmp(c.mac,stored.mac);
}
bool fresh(const Snapshot& s) {
    return s.received && s.link.current() && millis()-s.received_ms<=freshness_ms;
}
std::optional<std::string> read_status(const char* address,const Link& link) {
    IPAddress ip;
    if(!link.current() || !local_http_peer(address,ip))return {};
    const std::string url="http://"+std::string(address)+std::string(enevina::dsiot::endpoint);
    WiFiClient client;
    HTTPClient req;
    const char* headers[]={"Transfer-Encoding"};
    req.collectHeaders(headers,1);
    req.setReuse(false);
    req.setConnectTimeout(1100);
    req.setTimeout(1500);
    req.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!req.begin(client,url.c_str()))return {};
    req.addHeader("Content-Type","application/json");
    req.addHeader("Cache-Control","no-cache");
    auto body=status_request();
    const int status=req.POST(reinterpret_cast<uint8_t*>(body.data()),body.size());
    auto result=bounded_http_body(req,status,max_document,[&](){return link.current();});
    req.end();
    return result;
}
struct Poll {
    Snapshot sample{};
    const char* status="connection_failed";
};
Poll poll(const Config& config,const Link& link) {
    Poll result;
    const auto body=read_status(config.host,link);
    if(!body)return result;
    const auto reading=decode_status(*body);
    if(!reading) {result.status="unsupported";return result;}
    const auto& device=reading->device;
    if(config.mac[0] && device.mac!=config.mac) {result.status="device_changed";return result;}
    auto& out=result.sample;
    out.config=config;out.config.schema=config_schema;
    strlcpy(out.config.mac,device.mac.c_str(),sizeof(out.config.mac));
    out.reading=*reading;out.link=link;
    out.received_ms=millis();out.received_utc=time(nullptr);
    out.received=link.current();
    result.status=out.received?"ok":"link_changed";
    return result;
}
bool web_ready(WebServer& web,bool write) {
    if(!cottage_wifi_require_home_lan(web))return false;
    if(!gate) {web.send(503,"application/json","{}");return false;}
    if(write && (!web.hasArg("token") ||
       !enevina::cottage::c23_authorize_local_setting(web.arg("token").c_str()))) {
        web.send(403,"application/json","{\"error\":\"authorization\"}");return false;
    }
    return true;
}
void status(WebServer& web) {
    const bool draft=web.arg("draft")=="true";
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool staged=draft && state.stage_id && state.staged.link.current();
    const auto sample=staged?state.staged:state.active;
    const auto config=staged?state.staged.config:state.saved;
    const uint32_t id=staged?state.stage_id:0;
    const bool busy=state.busy || state.pending.task!=Task::None;
    const char* result=draft?state.draft_status:state.status;
    xSemaphoreGive(gate);
    auto* root=cJSON_CreateObject();
    if(!root) {web.send(503,"application/json","{}");return;}
    const bool current=fresh(sample);
    cJSON_AddBoolToObject(root,"configured",configured(config));
    // This age describes the module's HTTP response. Its indoor state may be cached.
    cJSON_AddBoolToObject(root,"fresh",current);
    cJSON_AddBoolToObject(root,"pump_side_fresh",false);
    cJSON_AddBoolToObject(root,"busy",busy);
    if(staged)cJSON_AddNumberToObject(root,"draft_id",id);
    cJSON_AddStringToObject(root,"result",result);
    if(configured(config)) {
        cJSON_AddStringToObject(root,"host",config.host);
        cJSON_AddStringToObject(root,"device",config.mac);
    }
    if(current) {
        cJSON_AddStringToObject(root,"firmware",sample.reading.device.firmware.c_str());
        if(!sample.reading.device.model.empty())cJSON_AddStringToObject(root,"model",sample.reading.device.model.c_str());
        cJSON_AddBoolToObject(root,"power",sample.reading.power);
        if(sample.reading.mode!=Mode::Unknown)
            cJSON_AddNumberToObject(root,"mode",static_cast<unsigned>(sample.reading.mode));
        if(!sample.reading.device.api_version.empty())
            cJSON_AddStringToObject(root,"api_version",sample.reading.device.api_version.c_str());
        if(sample.reading.target_c)cJSON_AddNumberToObject(root,"target_c",*sample.reading.target_c);
        if(sample.reading.indoor_c)cJSON_AddNumberToObject(root,"indoor_c",*sample.reading.indoor_c);
        if(sample.reading.outdoor_c)cJSON_AddNumberToObject(root,"outdoor_c",*sample.reading.outdoor_c);

    }
    char* data=cJSON_PrintUnformatted(root);
    web.send(data?200:503,"application/json",data?data:"{}");
    cJSON_free(data);
    cJSON_Delete(root);
}
void enqueue(WebServer& web,Task task,const char* host="") {
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool busy=state.busy || state.pending.task!=Task::None;
    if(!busy) {
        state.pending={task,live_link(),{},web.arg("draft")=="true"};
        if(task==Task::Identify && state.pending.draft) {state.staged={};state.stage_id=0;}
        strlcpy(state.pending.host,host,sizeof(state.pending.host));
    }
    xSemaphoreGive(gate);
    web.send(busy?409:202,"application/json",busy?
        "{\"error\":\"busy\"}":"{\"queued\":true}");
}
}

bool cottage_daikin_dsiot_selected() {
    return std::strcmp(site_settings_snapshot().pump_adapter,adapter_id)==0;
}
bool cottage_daikin_dsiot_needs_worker() {
    if(!gate)return false;
    if(cottage_daikin_dsiot_selected())return true;
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool pending=state.pending.task!=Task::None;
    xSemaphoreGive(gate);
    return pending;
}
void cottage_daikin_dsiot_register_routes(WebServer& web) {
    gate=xSemaphoreCreateMutex();
    if(!gate)return;
    Preferences p;
    Config saved{};
    if(p.begin("cottage_dso",true) && p.getBytesLength("link1")==sizeof(saved) &&
       p.getBytes("link1",&saved,sizeof(saved))==sizeof(saved) && configured(saved))
        state.saved=saved;
    web.on("/api/daikin-dsiot/status",HTTP_GET,[&web](){
        if(web_ready(web,false))status(web);
    });
    web.on("/api/daikin-dsiot/identify",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        IPAddress address;
        if(!local_http_peer(web.arg("host").c_str(),address)) {
            web.send(400,"application/json","{\"error\":\"invalid_address\"}");return;
        }
        const auto host=address.toString();
        enqueue(web,Task::Identify,host.c_str());
    });
    web.on("/api/daikin-dsiot/test",HTTP_POST,[&web](){
        if(web_ready(web,true))enqueue(web,Task::Read);
    });
    web.on("/api/daikin-dsiot/commit",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const String id=web.arg("draft_id");
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool matching=state.stage_id && id==String(state.stage_id) && fresh(state.staged) &&
            !state.busy && state.pending.task==Task::None;
        const bool saved=matching && persists(state.staged.config);
        if(saved) {
            state.saved=state.staged.config;
            state.active=std::move(state.staged);
            state.staged={};state.stage_id=0;
            ++state.revision;
            state.status="ok";
        }
        xSemaphoreGive(gate);
        web.send(saved?200:matching?500:409,"application/json",saved?
            "{\"saved\":true}":matching?"{\"error\":\"storage\"}":"{\"error\":\"connection_failed\"}");
    });
}

uint32_t cottage_daikin_dsiot_configuration_generation() {
    if(!gate)return 0;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto generation=state.revision;
    xSemaphoreGive(gate);
    return generation;
}
uint32_t cottage_daikin_dsiot_calibration_identity() {
    if(!gate)return 0;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto saved=state.saved;
    xSemaphoreGive(gate);
    if(!configured(saved))return 0;
    uint32_t hash=2166136261u;
    for(const char digit:saved.mac) {hash^=static_cast<uint8_t>(digit);hash*=16777619u;}
    return hash;
}
enevina::PumpRoomState cottage_daikin_dsiot_room_state() {
    using namespace enevina;
    PumpRoomState result{};
    if(!gate || !cottage_daikin_dsiot_selected())return result;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto snapshot=state.active;
    result.configuration_generation=state.revision;
    xSemaphoreGive(gate);
    if(!fresh(snapshot))return result;
    // Successful DSIoT requests can return the adapter's cached indoor values.
    // Retain them in the module's status endpoint, separate from control evidence.
    result.source=PumpStateSource::Gateway;
    result.protocol=PumpRoomProtocol::DaikinDsiot;
    result.type=HeatPumpType::AirToAir;
    result.endpoint_generation=snapshot.link.endpoint;result.link_generation=snapshot.link.wifi;
    if(snapshot.received_utc>=1700000000)result.evidence={snapshot.received_utc,0,false};
    return result;
}

void cottage_daikin_dsiot_tick() {
    static uint32_t last=0,last_endpoint=0,last_wifi=0;
    if(WiFi.status()!=WL_CONNECTED || cottage_ota_network_maintenance()) {
        return;
    }
    const auto link=live_link();
    const bool selected=cottage_daikin_dsiot_selected();
    xSemaphoreTake(gate,portMAX_DELAY);
    auto job=state.pending;
    if(job.task==Task::None && selected && configured(state.saved) &&
       (millis()-last>=read_interval_ms || link.endpoint!=last_endpoint || link.wifi!=last_wifi))
        job={Task::Read,link,{},false};
    const bool staged=job.draft && state.stage_id && state.staged.link.current();
    Config candidate=staged?state.staged.config:state.saved;
    state.pending={};
    state.busy=job.task!=Task::None;
    xSemaphoreGive(gate);
    if(job.task==Task::None) {return;}
    if(job.task==Task::Identify) {
        candidate={};
        strlcpy(candidate.host,job.host,sizeof(candidate.host));
    }
    Poll result{};
    if(job.link.current() && candidate.host[0]) {
        enevina_firmware::NetworkBudgetLease lease(700,72*1024,16*1024);
        if(lease.held())result=poll(candidate,job.link);
        else result.status="network_busy";
    }
    xSemaphoreTake(gate,portMAX_DELAY);
    state.busy=false;
    if(job.link.current()) {
        if(job.task==Task::Identify && result.sample.received) {
            if(job.draft) {
                if(++state.next_id==0)++state.next_id;
                state.staged=std::move(result.sample);state.stage_id=state.next_id;
            } else if(persists(result.sample.config)) {
                state.saved=result.sample.config;
                state.active=std::move(result.sample);
                state.staged={};state.stage_id=0;
                ++state.revision;
            } else result.status="storage";
        } else if(job.task==Task::Read) {
            // A failed active poll withdraws the previous reading. A
            // staged read retains the proposed address for explicit retry.
            if(staged) {
                if(result.sample.received)state.staged=std::move(result.sample);
                else state.staged.received=false;
            } else state.active=std::move(result.sample);
        }
    } else result.status="link_changed";
    (job.draft?state.draft_status:state.status)=result.status;
    xSemaphoreGive(gate);
    last=millis();last_endpoint=job.link.endpoint;last_wifi=job.link.wifi;
}
#endif
