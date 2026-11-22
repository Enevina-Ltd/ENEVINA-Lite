#ifndef COTTAGE_BENCH_MODE
#include "cottage_wf_rac_service.hpp"
#include "cottage_air_service.hpp"
#include "cottage_local_http.hpp"
#include "cottage_network_budget.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "pump/wf_rac.hpp"

#include <Preferences.h>
#include <WebServer.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <cstring>
#include <ctime>
#include <array>
#include <memory>

namespace {
using namespace enevina;
using namespace enevina::cottage;
using namespace enevina::wf_rac;
constexpr char adapter_id[]="mhi_wf_rac_lan";
constexpr uint32_t config_schema=1,read_interval_ms=30000,freshness_ms=90000;
constexpr size_t max_document=6144;
constexpr char info_command[]="getDeviceInfo",state_command[]="getAirconStat";

struct Config {
    uint32_t schema=0;
    char address[16]{},device[13]{};
};
struct Link {
    uint32_t endpoint=0,wifi=0;
    bool current() const {
        return WiFi.status()==WL_CONNECTED && site_settings_endpoint_generation()==endpoint &&
            cottage_wifi_link_generation()==wifi;
    }
};
struct Sample {
    Config target{};
    Link link{};
    Reading reading{};
    uint32_t received_ms=0;
    int64_t received_utc=0;
    char wireless_firmware[24]{},mcu_firmware[24]{};
    bool received=false;
};
enum class Task { None, Identify, Read };
struct Job {
    Task task=Task::None;
    Link link{};
    char address[16]{};
    bool draft=false;
};
struct State {
    Config saved{};
    Sample active{},draft{};
    Job pending{};
    uint32_t revision=0,draft_id=0,next_id=0;
    bool busy=false;
    const char* result="not_tested";
    const char* draft_result="not_tested";
};
SemaphoreHandle_t lock=nullptr;
State state{};

using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
using JsonText=std::unique_ptr<char,decltype(&cJSON_free)>;
Link current_link() {
    return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};
}
bool configured(const Config& c) {
    IPAddress ip;
    return c.schema==config_schema && std::memchr(c.address,0,sizeof(c.address)) &&
        std::memchr(c.device,0,sizeof(c.device)) && unit_id(c.device).has_value() &&
        ip.fromString(c.address);
}
bool persist(const Config& c) {
    Preferences prefs;
    if(!prefs.begin("cottage_wfr",false) ||
       prefs.putBytes("link1",&c,sizeof(c))!=sizeof(c))return false;
    Config stored{};
    return prefs.getBytes("link1",&stored,sizeof(stored))==sizeof(stored) && configured(stored) &&
        !std::strcmp(c.address,stored.address) && !std::strcmp(c.device,stored.device);
}
bool fresh(const Sample& sample) {
    return sample.received && millis()-sample.received_ms<=freshness_ms && sample.link.current();
}
const cJSON* member(const cJSON* object,const char* name) {
    const cJSON* match=nullptr;
    if(!cJSON_IsObject(object))return nullptr;
    for(auto* item=object->child;item;item=item->next) {
        if(!item->string || std::strcmp(item->string,name))continue;
        if(match)return nullptr;
        match=item;
    }
    return match;
}
const char* string_member(const cJSON* object,const char* name) {
    const auto* value=member(object,name);
    return cJSON_IsString(value)?value->valuestring:nullptr;
}
Json invoke(const char* address,const char* operation,const char* id,const Link& link) {
    Json response(nullptr,&cJSON_Delete);
    IPAddress ip;
    const auto utc=time(nullptr);
    if(!link.current() || utc<1700000000 || !local_http_peer(address,ip))return response;
    Json request(cJSON_CreateObject(),&cJSON_Delete);
    if(!request || !cJSON_AddStringToObject(request.get(),"apiVer","1.0") ||
       !cJSON_AddStringToObject(request.get(),"command",operation) ||
       !cJSON_AddStringToObject(request.get(),"deviceId","EnevinaLite") ||
       !cJSON_AddStringToObject(request.get(),"operatorId","enevina-lite-read") ||
       !cJSON_AddNumberToObject(request.get(),"timestamp",utc))return response;
    if(id) {
        auto* contents=cJSON_AddObjectToObject(request.get(),"contents");
        if(!contents || !cJSON_AddStringToObject(contents,"airconId",id))return response;
    }
    JsonText text(cJSON_PrintUnformatted(request.get()),&cJSON_free);
    if(!text)return response;
    const std::string url="http://"+std::string(address)+":51443/beaver/command/"+operation;
    WiFiClient client;
    HTTPClient http;
    const char* headers[]={"Transfer-Encoding"};
    http.collectHeaders(headers,1);
    http.setReuse(false);
    http.setConnectTimeout(1100);
    http.setTimeout(1300);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!http.begin(client,url.c_str()))return response;
    http.addHeader("Content-Type","application/json");
    http.addHeader("Cache-Control","no-cache");
    const int status=http.POST(reinterpret_cast<uint8_t*>(text.get()),std::strlen(text.get()));
    const auto body=bounded_http_body(http,status,max_document,[&](){return link.current();});
    http.end();
    if(!body || body->find('\0')!=body->npos)return response;
    response.reset(cJSON_ParseWithLengthOpts(body->c_str(),body->size()+1,nullptr,true));
    const auto* command=string_member(response.get(),"command");
    const auto* version=string_member(response.get(),"apiVer");
    const auto* code=member(response.get(),"result");
    if(!command || std::strcmp(command,operation) || !version || std::strcmp(version,"1.0") ||
       !cJSON_IsNumber(code) || code->valuedouble!=0 || !cJSON_IsObject(member(response.get(),"contents")))
        response.reset();
    return response;
}
std::optional<std::string> read_id(const cJSON* content,const char* key) {
    const auto* value=string_member(content,key);
    return value?unit_id(value):std::nullopt;
}
struct Poll {
    Sample sample{};
    const char* status="connection_failed";
};
Poll poll(const Config& config,const Link& link) {
    Poll result;
    if(time(nullptr)<1700000000) {result.status="clock_unavailable";return result;}
    const auto basic=invoke(config.address,info_command,nullptr,link);
    if(!basic)return result;
    const auto* info=member(basic.get(),"contents");
    const auto id=read_id(info,"airconId"),mac=read_id(info,"macAddress");
    const auto* firmware=string_member(info,"firmType");
    if(!id || !mac || id!=mac || (firmware && std::strcmp(firmware,"WF-RAC"))) {
        result.status="unsupported";return result;
    }
    if(config.device[0] && *id!=config.device) {result.status="device_changed";return result;}
    const auto aircon=invoke(config.address,state_command,id->c_str(),link);
    if(!aircon)return result;
    const auto* contents=member(aircon.get(),"contents");
    const auto reply_id=read_id(contents,"airconId");
    const auto* packed=string_member(contents,"airconStat");
    if(!reply_id || reply_id!=id || !packed || std::strlen(packed)>4*((max_frame+2)/3)) {
        result.status="invalid_status";return result;
    }
    std::array<unsigned char,max_frame> data{};
    size_t length=0;
    if(mbedtls_base64_decode(data.data(),data.size(),&length,
       reinterpret_cast<const unsigned char*>(packed),std::strlen(packed))!=0) {
        result.status="invalid_status";return result;
    }
    const auto reading=decode({reinterpret_cast<const char*>(data.data()),length});
    if(!reading) {result.status="invalid_status";return result;}
    auto& sample=result.sample;
    sample.target=config;
    sample.target.schema=config_schema;
    strlcpy(sample.target.device,id->c_str(),sizeof(sample.target.device));
    if(const auto* ver=string_member(member(info,"wireless"),"firmVer"))
        strlcpy(sample.wireless_firmware,ver,sizeof(sample.wireless_firmware));
    if(const auto* ver=string_member(member(info,"mcu"),"firmVer"))
        strlcpy(sample.mcu_firmware,ver,sizeof(sample.mcu_firmware));
    sample.link=link;
    sample.reading=*reading;
    sample.received_ms=millis();
    sample.received_utc=time(nullptr);
    sample.received=link.current();
    result.status=sample.received?"ok":"link_changed";
    return result;
}
bool ready(WebServer& web,bool mutation) {
    if(!cottage_wifi_require_home_lan(web))return false;
    if(!lock) {web.send(503,"application/json","{}");return false;}
    if(mutation && (!web.hasArg("token") ||
       !c23_authorize_local_setting(web.arg("token").c_str()))) {
        web.send(403,"application/json","{\"error\":\"authorization\"}");return false;
    }
    return true;
}
void status(WebServer& web) {
    const bool draft=web.arg("draft")=="true";
    xSemaphoreTake(lock,portMAX_DELAY);
    const bool display_draft=draft && state.draft_id && state.draft.link.current();
    const auto sample=display_draft?state.draft:state.active;
    const auto config=display_draft?state.draft.target:state.saved;
    const auto draft_id=display_draft?state.draft_id:0;
    const auto error=draft?state.draft_result:state.result;
    const bool busy=state.busy || state.pending.task!=Task::None;
    xSemaphoreGive(lock);

    Json root(cJSON_CreateObject(),&cJSON_Delete);
    if(!root) {web.send(503,"application/json","{}");return;}
    const bool current=fresh(sample);
    cJSON_AddBoolToObject(root.get(),"configured",configured(config));
    cJSON_AddBoolToObject(root.get(),"fresh",current);
    cJSON_AddBoolToObject(root.get(),"busy",busy);
    cJSON_AddStringToObject(root.get(),"result",error);
    if(draft_id)cJSON_AddNumberToObject(root.get(),"draft_id",draft_id);
    if(configured(config)) {
        cJSON_AddStringToObject(root.get(),"address",config.address);
        cJSON_AddStringToObject(root.get(),"device",config.device);
    }
    if(current) {
        const auto& reading=sample.reading;
        cJSON_AddNumberToObject(root.get(),"model_number",reading.model_number);
        cJSON_AddBoolToObject(root.get(),"power",reading.power);
        if(reading.mode!=Mode::Unknown)
            cJSON_AddNumberToObject(root.get(),"mode",static_cast<unsigned>(reading.mode));
        if(reading.target_c)cJSON_AddNumberToObject(root.get(),"target_c",*reading.target_c);
        if(reading.indoor_c)cJSON_AddNumberToObject(root.get(),"indoor_c",*reading.indoor_c);
        if(reading.outdoor_c)cJSON_AddNumberToObject(root.get(),"outdoor_c",*reading.outdoor_c);
        cJSON_AddBoolToObject(root.get(),"compressor_running",reading.compressor_running);
        if(sample.wireless_firmware[0])
            cJSON_AddStringToObject(root.get(),"wireless_firmware",sample.wireless_firmware);
        if(sample.mcu_firmware[0])
            cJSON_AddStringToObject(root.get(),"mcu_firmware",sample.mcu_firmware);
    }
    JsonText data(cJSON_PrintUnformatted(root.get()),&cJSON_free);
    web.send(data?200:503,"application/json",data?data.get():"{}");
}
void enqueue(WebServer& web,Task task,const char* address="") {
    xSemaphoreTake(lock,portMAX_DELAY);
    const bool busy=state.busy || state.pending.task!=Task::None;
    if(!busy) {
        state.pending={task,current_link(),{},web.arg("draft")=="true"};
        if(task==Task::Identify && state.pending.draft) {
            state.draft={};state.draft_id=0;
        }
        strlcpy(state.pending.address,address,sizeof(state.pending.address));
    }
    xSemaphoreGive(lock);
    web.send(busy?409:202,"application/json",busy?
        "{\"error\":\"busy\"}":"{\"queued\":true}");
}
void activate(Sample sample) {
    state.saved=sample.target;
    state.active=sample;
    state.draft={};state.draft_id=0;
    ++state.revision;
    state.result="ok";
}
}

bool cottage_wf_rac_selected() {
    return !std::strcmp(site_settings_snapshot().pump_adapter,adapter_id);
}
bool cottage_wf_rac_needs_worker() {
    if(!lock)return false;
    if(cottage_wf_rac_selected())return true;
    xSemaphoreTake(lock,portMAX_DELAY);
    const bool pending=state.pending.task!=Task::None;
    xSemaphoreGive(lock);
    return pending;
}
void cottage_wf_rac_register_routes(WebServer& web) {
    lock=xSemaphoreCreateMutex();
    if(!lock)return;
    Preferences prefs;
    Config stored{};
    if(prefs.begin("cottage_wfr",true) && prefs.getBytesLength("link1")==sizeof(stored) &&
       prefs.getBytes("link1",&stored,sizeof(stored))==sizeof(stored) && configured(stored))
        state.saved=stored;
    web.on("/api/wf-rac/status",HTTP_GET,[&web](){
        if(ready(web,false))status(web);
    });
    web.on("/api/wf-rac/identify",HTTP_POST,[&web](){
        if(!ready(web,true))return;
        IPAddress address;
        if(!local_http_peer(web.arg("address").c_str(),address)) {
            web.send(400,"application/json","{\"error\":\"invalid_address\"}");return;
        }
        const auto peer=address.toString();
        enqueue(web,Task::Identify,peer.c_str());
    });
    web.on("/api/wf-rac/test",HTTP_POST,[&web](){
        if(ready(web,true))enqueue(web,Task::Read);
    });
    web.on("/api/wf-rac/commit",HTTP_POST,[&web](){
        if(!ready(web,true))return;
        const String expected=web.arg("draft_id");
        xSemaphoreTake(lock,portMAX_DELAY);
        const bool valid=state.draft_id && expected==String(state.draft_id) && fresh(state.draft) &&
            !state.busy && state.pending.task==Task::None;
        const bool saved=valid && persist(state.draft.target);
        if(saved)activate(state.draft);
        xSemaphoreGive(lock);
        web.send(saved?200:valid?500:409,"application/json",saved?
            "{\"saved\":true}":valid?"{\"error\":\"storage\"}":"{\"error\":\"connection_failed\"}");
    });
}
uint32_t cottage_wf_rac_configuration_generation() {
    if(!lock)return 0;
    xSemaphoreTake(lock,portMAX_DELAY);
    const uint32_t revision=state.revision;
    xSemaphoreGive(lock);
    return revision;
}
uint32_t cottage_wf_rac_calibration_identity() {
    if(!lock)return 0;
    xSemaphoreTake(lock,portMAX_DELAY);
    const auto saved=state.saved;
    xSemaphoreGive(lock);
    if(!configured(saved))return 0;
    uint32_t hash=2166136261u;
    for(const char digit:saved.device) {hash^=static_cast<uint8_t>(digit);hash*=16777619u;}
    return hash;
}
PumpRoomState cottage_wf_rac_room_state() {
    PumpRoomState result{};
    if(!lock || !cottage_wf_rac_selected())return result;
    xSemaphoreTake(lock,portMAX_DELAY);
    const auto sample=state.active;
    result.configuration_generation=state.revision;
    xSemaphoreGive(lock);
    if(!fresh(sample))return result;
    result.source=PumpStateSource::DirectLan;
    result.protocol=PumpRoomProtocol::MhiWfRac;
    result.type=HeatPumpType::AirToAir;
    result.endpoint_generation=sample.link.endpoint;result.link_generation=sample.link.wifi;
    result.mode=heating_mode(sample.reading);
    if(sample.reading.target_c)result.setpoint_c=*sample.reading.target_c;
    if(sample.reading.indoor_c)result.indoor_c=*sample.reading.indoor_c;
    if(sample.reading.outdoor_c)result.outdoor_c=*sample.reading.outdoor_c;
    if(sample.received_utc>=1700000000)result.evidence={sample.received_utc,0,false};
    return result;
}
void cottage_wf_rac_tick() {
    static uint32_t last=0,last_endpoint=0,last_wifi=0;
    if(WiFi.status()!=WL_CONNECTED || cottage_ota_network_maintenance())return;
    const auto link=current_link();
    const bool selected=cottage_wf_rac_selected();
    xSemaphoreTake(lock,portMAX_DELAY);
    auto job=state.pending;
    if(job.task==Task::None && selected && configured(state.saved) &&
       (millis()-last>=read_interval_ms || link.endpoint!=last_endpoint || link.wifi!=last_wifi))
        job={Task::Read,link};
    const bool draft=job.draft && state.draft_id && state.draft.link.current();
    Config candidate=draft?state.draft.target:state.saved;
    state.pending={};
    state.busy=job.task!=Task::None;
    xSemaphoreGive(lock);
    if(job.task==Task::None)return;
    if(job.task==Task::Identify) {
        candidate={};
        strlcpy(candidate.address,job.address,sizeof(candidate.address));
    }
    Poll result;
    if(job.link.current() && candidate.address[0]) {
        enevina_firmware::NetworkBudgetLease lease(700,72*1024,16*1024);
        if(lease.held())result=poll(candidate,job.link);
        else result.status="network_busy";
    }
    xSemaphoreTake(lock,portMAX_DELAY);
    state.busy=false;
    if(job.link.current()) {
        if(job.task==Task::Identify && result.sample.received) {
            if(job.draft) {
                if(++state.next_id==0)++state.next_id;
                state.draft_id=state.next_id;
                state.draft=result.sample;
            } else if(persist(result.sample.target))activate(result.sample);
            else result.status="storage";
        } else if(job.task==Task::Read) {
            if(draft) {
                if(result.sample.received)state.draft=result.sample;
                else state.draft.received=false;
            } else state.active=result.sample;
        }
    } else result.status="link_changed";
    (job.draft?state.draft_result:state.result)=result.status;
    xSemaphoreGive(lock);
    last=millis();last_endpoint=job.link.endpoint;last_wifi=job.link.wifi;
}
#endif
