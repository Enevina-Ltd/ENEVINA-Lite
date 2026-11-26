#ifndef COTTAGE_BENCH_MODE
#include "cottage_wf_rac_service.hpp"
#include "cottage/automatic_command_reconciliation.hpp"
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
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <memory>

namespace {
using namespace enevina;
using namespace enevina::cottage;
using namespace enevina::wf_rac;
constexpr char adapter_id[]="mhi_wf_rac_lan";
constexpr uint32_t config_schema=1,read_interval_ms=30000,freshness_ms=90000;
constexpr size_t max_document=6144;
constexpr char info_command[]="getDeviceInfo",state_command[]="getAirconStat",
    write_command[]="setAirconStat";

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
enum class Task { None, Identify, Read, Write };
struct Command {
    Patch patch{};
    Reading previous{};
    BootstrapDecision proposal{};
    uint32_t revision=0,authorization=0,config=0,settings=0;
    int64_t queued_utc=0;
    bool automatic=false;
};
struct Receipt {
    Patch patch{};
    int64_t utc=0;
};
struct Approval {
    PumpHeatingLimits limits{};
    Sample pump{};
    char operator_id[37]{};
    char model[48]{};
    uint32_t revision=0;
};
struct Job {
    Task task=Task::None;
    Link link{};
    char address[16]{};
    bool draft=false;
    Command command{};
};
struct State {
    Config saved{};
    Sample active{},draft{};
    Job pending{};
    uint32_t draft_id=0,next_id=0;
    bool busy=false;
    bool write_active=false;
    uint32_t write_generation=0;
    const char* result="not_tested";
    const char* draft_result="not_tested";
    Approval approval{};
    MitsubishiModeHistory dwell{};
    AutomaticCommandReconciliation reconciliation{};
    uint32_t request_generation=0,mode_since=0,request_since=0;
    int64_t automatic_queued_utc=0,automatic_confirmed_utc=0;
    Receipt requested{},sent{},confirmed{};
};
SemaphoreHandle_t lock=nullptr;
State state{};
std::atomic<uint32_t> revision{0};

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
bool same_pump(const Sample& a,const Sample& b) {
    return !std::strcmp(a.target.device,b.target.device) &&
        a.reading.model_number==b.reading.model_number &&
        !std::strcmp(a.wireless_firmware,b.wireless_firmware) &&
        !std::strcmp(a.mcu_firmware,b.mcu_firmware);
}
bool pilot_approved() {
    const auto& permission=state.approval;
    return permission.limits.heating && permission.revision==revision.load() &&
        permission.pump.link.current() && same_pump(state.active,permission.pump);
}
bool can_queue() {
    return pilot_approved() && fresh(state.active) && !state.busy &&
        state.pending.task==Task::None && permits_heat(state.active.reading,{});
}
void observe(const Sample& sample) {
    if(sample.received) {
        const auto now=sample.received_ms;
        const bool continuous=state.active.received && same_pump(sample,state.active) &&
            sample.link.endpoint==state.active.link.endpoint && sample.link.wifi==state.active.link.wifi &&
            now-state.active.received_ms<=MitsubishiModeHistory::observation_gap_ms;
        if(!continuous || !same_request(sample.reading,state.active.reading)) {
            ++state.request_generation;
            state.request_since=now;
            state.automatic_queued_utc=state.automatic_confirmed_utc=0;
        }
        if(!continuous || sample.reading.power!=state.active.reading.power ||
           sample.reading.mode!=state.active.reading.mode) {
            state.mode_since=now;
            state.dwell.command_dispatched(now);
        }
        state.dwell.observe(sample.reading.power,now,true);
    }
    state.active=sample;
}
bool authorization_current(const Job& job) {
    const auto& cmd=job.command;
    if(!job.link.current() || cmd.revision!=revision.load() || !cottage_wf_rac_selected() ||
       cottage_ota_network_maintenance())return false;
    return cmd.automatic?
        cottage_wf_rac_automatic_write_current(cmd.authorization,cmd.config,cmd.settings,cmd.proposal):
        !cottage_wf_rac_automatic_enabled();
}
void queue(Command command) {
    command.queued_utc=time(nullptr);
    ++state.write_generation;
    state.pending={Task::Write,state.active.link,{},false,command};
    state.requested={command.patch,command.queued_utc};
    state.sent={};
    state.result="write_queued";
}
std::optional<double> parse_target_c(const String& value) {
    if(value.length()<2 || value.length()>4)return {};
    const char* input=value.c_str();
    if(input[0]<'0' || input[0]>'9' || input[1]<'0' || input[1]>'9' ||
       (value.length()!=2 && (value.length()!=4 || input[2]!='.' ||
        (input[3]!='0' && input[3]!='5'))))return {};
    const double parsed=std::strtod(input,nullptr);
    return parsed>=10 && parsed<=33?std::optional<double>{parsed}:std::nullopt;
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
Json invoke(const char* address,const char* operation,const char* id,const Link& link,
            const char* packed=nullptr,const Approval* approval=nullptr,
            const Job* job=nullptr,bool mode_changed=false,bool* attempted=nullptr) {
    Json response(nullptr,&cJSON_Delete);
    IPAddress ip;
    const auto utc=time(nullptr);
    if(!link.current() || utc<1700000000 || !local_http_peer(address,ip))return response;
    char client_id[32]{};
    std::snprintf(client_id,sizeof(client_id),"EnevinaLite-%08lx",
                  static_cast<unsigned long>(ESP.getEfuseMac()&0xffffffffu));
    Json request(cJSON_CreateObject(),&cJSON_Delete);
    if(!request || !cJSON_AddStringToObject(request.get(),"apiVer","1.0") ||
       !cJSON_AddStringToObject(request.get(),"command",operation) ||
       !cJSON_AddStringToObject(request.get(),"deviceId",client_id) ||
       !cJSON_AddStringToObject(request.get(),"operatorId",approval?approval->operator_id:"enevina-lite-read") ||
       !cJSON_AddNumberToObject(request.get(),"timestamp",utc))return response;
    if(id) {
        auto* contents=cJSON_AddObjectToObject(request.get(),"contents");
        if(!contents || !cJSON_AddStringToObject(contents,"airconId",id) ||
           (packed && !cJSON_AddStringToObject(contents,"airconStat",packed)))return response;
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
    if(job) {
        if(!authorization_current(*job))return response;
        xSemaphoreTake(lock,portMAX_DELAY);
        const bool current=job->link.current() && pilot_approved() &&
            job->command.revision==revision.load();
        if(current) {
            state.sent={job->command.patch,time(nullptr)};
            if(mode_changed)state.dwell.command_dispatched(millis());
        }
        xSemaphoreGive(lock);
        if(!current)return response;
        if(attempted)*attempted=true;
    }
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
struct WriteResult {
    const char* status="write_cancelled";
    Sample sample{};
    bool observed=false,confirmed=false,attempted=false;
};
WriteResult perform_write(const Job& job,const Config& target,const Approval& approval) {
    WriteResult result;
    if(!authorization_current(job))return result;
    auto before=poll(target,job.link);
    result.observed=true;
    if(!before.sample.received) {result.status=before.status;return result;}
    result.sample=before.sample;
    if(!same_pump(result.sample,approval.pump)) {
        result.status="device_changed";return result;
    }
    const auto& cmd=job.command;
    const auto& reading=before.sample.reading;
    const bool mode_changed=changes_mode(reading,cmd.patch);
    xSemaphoreTake(lock,portMAX_DELAY);
    observe(result.sample);
    const bool dwell_allowed=!mode_changed || state.dwell.since_change(millis())>=mitsubishi_min_mode_dwell_ms;
    xSemaphoreGive(lock);
    if((cmd.automatic || (!cmd.patch.power && !cmd.patch.heat)) && !same_request(reading,cmd.previous)) {
        result.status="external_change";return result;
    }
    if(!permits_heat(reading,cmd.patch)) {result.status="unsupported_command";return result;}
    if(readback_matches(reading,reading,cmd.patch)) {result.status="already_applied";return result;}
    if(!dwell_allowed) {result.status="mode_dwell";return result;}
    const auto frame=command(cmd.patch,approval.limits);
    if(!frame) {result.status="unsupported_command";return result;}
    std::array<unsigned char,4*((max_frame+2)/3)+1> encoded{};
    size_t count=0;
    if(mbedtls_base64_encode(encoded.data(),encoded.size(),&count,
       reinterpret_cast<const unsigned char*>(frame->data()),frame->size())!=0 ||
       count>=encoded.size())return result;
    bool attempted=false;
    const auto ack=invoke(target.address,write_command,target.device,job.link,
        reinterpret_cast<const char*>(encoded.data()),&approval,&job,mode_changed,&attempted);
    result.attempted=attempted;
    if(!attempted)return result;
    result.status=ack?"unconfirmed":"send_failed";
    result.sample={};
    // A new getAirconStat after the attempted write is the only confirmation.
    for(unsigned attempt=0;attempt<3 && job.link.current();++attempt) {
        vTaskDelay(pdMS_TO_TICKS(500));
        auto after=poll(target,job.link);
        if(!after.sample.received){result.sample={};continue;}
        result.sample=after.sample;
        if(!same_pump(result.sample,approval.pump)) {result.status="device_changed";break;}
        if(readback_matches(reading,result.sample.reading,cmd.patch) && authorization_current(job)) {
            result.status="confirmed";result.confirmed=true;break;
        }
    }
    return result;
}
void receipt(cJSON* root,const char* key,const Receipt& value) {
    if(value.utc<1700000000)return;
    auto* out=cJSON_AddObjectToObject(root,key);
    if(!out)return;
    cJSON_AddNumberToObject(out,"utc",value.utc);
    if(value.patch.power)cJSON_AddBoolToObject(out,"power",*value.patch.power);
    if(value.patch.target_c)cJSON_AddNumberToObject(out,"target_c",*value.patch.target_c);
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
    const bool pilot=!draft && cottage_wf_rac_selected() && pilot_approved();
    const bool writable=pilot && can_queue() && !cottage_wf_rac_automatic_enabled();
    const auto limits=state.approval.limits;
    const auto requested=state.requested,sent=state.sent,confirmed=state.confirmed;
    const auto generation=revision.load();
    xSemaphoreGive(lock);

    Json root(cJSON_CreateObject(),&cJSON_Delete);
    if(!root) {web.send(503,"application/json","{}");return;}
    const bool current=fresh(sample);
    cJSON_AddBoolToObject(root.get(),"configured",configured(config));
    cJSON_AddBoolToObject(root.get(),"fresh",current);
    cJSON_AddBoolToObject(root.get(),"busy",busy);
    cJSON_AddStringToObject(root.get(),"result",error);
    if(draft_id)cJSON_AddNumberToObject(root.get(),"draft_id",draft_id);
    if(!draft) {
        cJSON_AddNumberToObject(root.get(),"revision",generation);
        cJSON_AddBoolToObject(root.get(),"pilot_approved",pilot);
        cJSON_AddBoolToObject(root.get(),"write_available",writable);
        if(pilot) {
            cJSON_AddNumberToObject(root.get(),"minimum_c",limits.minimum_c);
            cJSON_AddNumberToObject(root.get(),"maximum_c",limits.maximum_c);
            cJSON_AddNumberToObject(root.get(),"step_c",limits.step_c);
            receipt(root.get(),"requested",requested);
            receipt(root.get(),"sent",sent);
            receipt(root.get(),"confirmed",confirmed);
        }
    }
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
        cJSON_AddBoolToObject(root.get(),"compressor_demand",reading.compressor_demand);
        cJSON_AddBoolToObject(root.get(),"self_clean",reading.self_clean);
        cJSON_AddNumberToObject(root.get(),"fault_code",reading.fault);
        if(sample.wireless_firmware[0])
            cJSON_AddStringToObject(root.get(),"wireless_firmware",sample.wireless_firmware);
        if(sample.mcu_firmware[0])
            cJSON_AddStringToObject(root.get(),"mcu_firmware",sample.mcu_firmware);
    }
    JsonText data(cJSON_PrintUnformatted(root.get()),&cJSON_free);
    web.send_P(data?200:503,"application/json",data?data.get():"{}");
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
    state.active={};
    observe(sample);
    state.draft={};state.draft_id=0;
    state.approval={};state.requested={};state.sent={};state.confirmed={};
    revision.fetch_add(1);
    cottage_wf_rac_automatic_cancel();
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
    web.on("/api/wf-rac/pilot",HTTP_POST,[&web](){
        if(!ready(web,true))return;
        const String enabled=web.arg("enabled"),device=web.arg("device"),expected=web.arg("revision");
        const String name=web.arg("model"),operator_id=web.arg("operator");
        const auto low=parse_target_c(web.arg("min")),high=parse_target_c(web.arg("max"));
        const bool requested=enabled=="true";
        if((!requested && enabled!="false") ||
           (requested && (!low || !high || *low>=*high || name.length()<3 ||
                          name.length()>47 || !operator_id_valid(operator_id.c_str())))) {
            web.send(400,"application/json","{\"error\":\"model_limits\"}");return;
        }
        xSemaphoreTake(lock,portMAX_DELAY);
        const bool allowed=cottage_wf_rac_selected() && !state.busy && state.pending.task==Task::None &&
            (!requested || (expected==String(revision.load()) && fresh(state.active) &&
                            device==state.active.target.device));
        if(allowed) {
            cottage_wf_rac_automatic_cancel();
            state.approval={};state.requested={};state.sent={};state.confirmed={};
            state.automatic_queued_utc=state.automatic_confirmed_utc=0;
            if(requested) {
                state.approval.limits={*low,*high,.5,true,true};
                state.approval.revision=revision.load();
                state.approval.pump=state.active;
                strlcpy(state.approval.model,name.c_str(),sizeof(state.approval.model));
                strlcpy(state.approval.operator_id,operator_id.c_str(),sizeof(state.approval.operator_id));
            }
            state.result=requested?"pilot_approved":"ok";
        }
        xSemaphoreGive(lock);
        web.send(allowed?200:409,"application/json",allowed?
            "{\"saved\":true}":"{\"error\":\"pilot_required\"}");
    });
    web.on("/api/wf-rac/control",HTTP_POST,[&web](){
        if(!ready(web,true))return;
        const String action=web.arg("action"),expected=web.arg("revision");
        Patch patch;
        if(action=="off")patch.power=false;
        else if(action=="heat" || action=="target") {
            patch.target_c=parse_target_c(web.arg("target"));
            if(action=="heat") {patch.power=true;patch.heat=true;}
        }
        if(action!="off" && !patch.target_c) {
            web.send(400,"application/json","{\"error\":\"unsupported_command\"}");return;
        }
        xSemaphoreTake(lock,portMAX_DELAY);
        const auto& reading=state.active.reading;
        const bool ready=cottage_wf_rac_selected() && !cottage_wf_rac_automatic_enabled() &&
            expected==String(revision.load()) && can_queue();
        const bool supported=ready && permits_heat(reading,patch) &&
            command(patch,state.approval.limits).has_value();
        const bool allowed=supported && (!changes_mode(reading,patch) ||
            state.dwell.since_change(millis())>=mitsubishi_min_mode_dwell_ms);
        if(allowed) {
            Command cmd{};
            cmd.patch=patch;cmd.previous=reading;cmd.revision=revision.load();
            queue(cmd);
        }
        xSemaphoreGive(lock);
        web.send(allowed?202:409,"application/json",allowed?"{\"queued\":true}":
            !ready?"{\"error\":\"pilot_required\"}":!supported?"{\"error\":\"unsupported_command\"}":
            "{\"error\":\"mode_dwell\"}");
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
uint32_t cottage_wf_rac_configuration_generation() {return revision.load();}
bool cottage_wf_rac_automatic_ready() {
    if(!lock || !cottage_wf_rac_selected())return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    const auto mode=heating_mode(state.active.reading);
    const bool result=can_queue() && (mode==PumpRoomMode::Off || mode==PumpRoomMode::Heat);
    xSemaphoreGive(lock);
    return result;
}
ControlBlock cottage_wf_rac_submit_automatic(const PumpRoomState& earlier,ControlSnapshot proposal,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    if(!lock || !cottage_wf_rac_automatic_enabled())return ControlBlock::Disabled;
    xSemaphoreTake(lock,portMAX_DELAY);
    ControlBlock result=ControlBlock::InputsUnverified;
    const bool usable=can_queue();
    if(state.reconciliation.awaiting(authorization)) {
        xSemaphoreGive(lock);return ControlBlock::AwaitingReadback;
    }
    if(usable && earlier.configuration_generation==revision.load() &&
       earlier.request_generation==state.request_generation &&
       earlier.endpoint_generation==state.active.link.endpoint &&
       earlier.link_generation==state.active.link.wifi) {
        const auto& reading=state.active.reading;
        const auto mode=heating_mode(reading);
        const auto limits=state.approval.limits;
        if(mode==PumpRoomMode::Off || mode==PumpRoomMode::Heat) {
            const auto initial=proposal.proposal;
            proposal.cn105_link_fresh=proposal.bridge_get_ok=true;
            proposal.current_heat=mode==PumpRoomMode::Heat;
            proposal.current_setpoint_c=reading.target_c.value_or(NAN);
            proposal.ms_since_mode_change=state.dwell.since_change(millis());
            const auto decision=choose_pump_control(proposal,limits,HeatTransition::Combined);
            result=decision.gate;
            if(decision.step!=ControlStep::None) {
                Command cmd{};
                cmd.proposal=initial;cmd.previous=reading;cmd.automatic=true;
                cmd.revision=revision.load();cmd.authorization=authorization;
                cmd.config=config;cmd.settings=settings;
                if(decision.step==ControlStep::SetOff)cmd.patch.power=false;
                else {
                    cmd.patch.target_c=decision.target_c;
                    if(decision.step==ControlStep::SetHeat) {cmd.patch.power=true;cmd.patch.heat=true;}
                }
                queue(cmd);
            }
        }
    }
    xSemaphoreGive(lock);
    return result;
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
    result.configuration_generation=revision.load();
    result.request_generation=state.request_generation;
    result.command_unconfirmed=state.reconciliation.awaiting(cottage_control_authorization_generation());
    result.write_generation=state.write_generation;
    result.write_in_progress=state.write_active || state.pending.task==Task::Write;
    result.mode_minutes=(millis()-state.mode_since)/60000;
    result.request_minutes=(millis()-state.request_since)/60000;
    result.automatic_queued_utc=state.automatic_queued_utc;
    result.automatic_confirmed_utc=state.automatic_confirmed_utc;
    if(pilot_approved() && permits_heat(sample.reading,{}))result.heating=state.approval.limits;
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
    const auto approval=state.approval;
    state.pending={};
    state.busy=job.task!=Task::None;
    state.write_active=job.task==Task::Write;
    xSemaphoreGive(lock);
    if(job.task==Task::None)return;
    if(job.task==Task::Identify) {
        candidate={};
        strlcpy(candidate.address,job.address,sizeof(candidate.address));
    }
    Poll result;
    WriteResult written;
    if(job.link.current() && candidate.address[0]) {
        enevina_firmware::NetworkBudgetLease lease(700,72*1024,16*1024);
        if(lease.held()) {
            if(job.task==Task::Write)written=perform_write(job,candidate,approval);
            else result=poll(candidate,job.link);
        } else {
            result.status="network_busy";
            written.status="network_busy";
        }
    }
    xSemaphoreTake(lock,portMAX_DELAY);
    state.busy=false;
    state.write_active=false;
    if(job.task==Task::Write && job.command.automatic && written.attempted &&
       !written.confirmed && job.command.revision==revision.load()) {
        const auto& wanted=job.command.patch;
        state.automatic_queued_utc=state.automatic_confirmed_utc=0;
        ++state.request_generation;state.request_since=millis();
        state.reconciliation.unresolved(job.command.authorization,
            wanted.power==false?PumpRoomMode::Off:PumpRoomMode::Heat,
            wanted.target_c.has_value(),wanted.target_c.value_or(0));
    }
    if(job.link.current()) {
        if(job.task==Task::Write) {
            result.status=written.status;
            if(cottage_wf_rac_selected() && job.command.revision==revision.load()) {
                if(written.observed)observe(written.sample);
                if(written.confirmed) {
                    state.confirmed={job.command.patch,state.active.received_utc};
                    if(job.command.automatic) {
                        state.automatic_queued_utc=job.command.queued_utc;
                        state.automatic_confirmed_utc=state.active.received_utc;
                    }
                }
            }
        } else if(job.task==Task::Identify && result.sample.received) {
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
            } else {
                observe(result.sample);
                if(!job.draft && selected && state.reconciliation.observe(
                        cottage_control_authorization_generation(),can_queue(),
                        heating_mode(state.active.reading),state.active.reading.target_c.value_or(NAN))) {
                    ++state.request_generation;state.request_since=millis();
                    state.automatic_queued_utc=state.automatic_confirmed_utc=0;
                }
            }
        }
    } else result.status="link_changed";
    (job.draft?state.draft_result:state.result)=result.status;
    xSemaphoreGive(lock);
    last=millis();last_endpoint=job.link.endpoint;last_wifi=job.link.wifi;
}
#endif
