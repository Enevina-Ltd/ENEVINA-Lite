#ifndef COTTAGE_BENCH_MODE
#include "cottage_daikin_service.hpp"
#include "cottage_air_service.hpp"
#include "cottage_network_budget.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "pump/daikin_legacy.hpp"

#include <HTTPClient.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cJSON.h>
#include <cstring>
#include <ctime>

namespace {
using namespace enevina;
using namespace enevina::cottage;
using namespace enevina::daikin;
constexpr char adapter_id[]="daikin_local_api";
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
    Device device{};
    Reading reading{};
    Temperatures temperatures{};
    Link link{};
    uint32_t received_ms=0;
    int64_t received_utc=0;
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
    bool present=false;
};
struct Work {
    Task task=Task::None;
    Link link{};
    char host[16]{};
    bool draft=false;
    Command command{};
};
struct State {
    Config saved{};
    Snapshot active{},staged{};
    uint32_t revision=0,stage_id=0,next_id=0;
    Work pending{};
    bool busy=false;
    const char* status="not_tested";
    const char* draft_status="not_tested";
    PumpHeatingLimits limits{};
    Device approved_device{};
    char approved_model[48]{};
    Link approval_link{};
    uint32_t approval_revision=0,request_generation=0,request_since_ms=0,mode_since_ms=0;
    int64_t automatic_queued_utc=0,automatic_confirmed_utc=0;
    MitsubishiModeHistory dwell{};
    Receipt requested{},sent{},confirmed{};
};
SemaphoreHandle_t gate=nullptr;
State state{};
std::atomic<uint32_t> configuration_revision{0};

Link live_link() {
    return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};
}
bool local_host(const char* text,IPAddress& address) {
    if(!text || !address.fromString(text))return false;
    const auto a=address[0],b=address[1];
    if(!(a==10 || (a==192 && b==168) || (a==172 && b>=16 && b<=31)))return false;
    const auto host=static_cast<uint32_t>(WiFi.localIP());
    const auto mask=static_cast<uint32_t>(WiFi.subnetMask());
    const auto peer=static_cast<uint32_t>(address);
    const auto network=host&mask;
    return host && mask && (peer&mask)==network && peer!=host && peer!=network && peer!=(network|~mask);
}
bool configured(const Config& config) {
    if(config.schema!=config_schema || !std::memchr(config.host,0,sizeof(config.host)) ||
       !std::memchr(config.mac,0,sizeof(config.mac)) || std::strlen(config.mac)!=12)return false;
    IPAddress address;
    return address.fromString(config.host);
}
bool persists(const Config& c) {
    Preferences p;
    if(!p.begin("cottage_dai",false) || p.putBytes("legacy1",&c,sizeof(c))!=sizeof(c))return false;
    Config stored{};
    return p.getBytes("legacy1",&stored,sizeof(stored))==sizeof(stored) &&
        stored.schema==c.schema && !std::strcmp(c.host,stored.host) &&
        !std::strcmp(c.mac,stored.mac);
}
bool fresh(const Snapshot& s) {
    return s.received && s.link.current() && millis()-s.received_ms<=freshness_ms;
}
bool same_device(const Device& a,const Device& b) {
    return a.mac==b.mac && a.firmware==b.firmware && a.model==b.model;
}
bool approved() {
    return state.limits.heating && state.approval_link.current() &&
        state.approval_revision==configuration_revision.load() &&
        same_device(state.approved_device,state.active.device);
}
bool available() {
    return approved() && fresh(state.active) && !state.busy && state.pending.task==Task::None;
}
void observe(Snapshot sample) {
    if(sample.received) {
        const auto now=sample.received_ms;
        const bool continuous=state.active.received && state.active.link.endpoint==sample.link.endpoint &&
            state.active.link.wifi==sample.link.wifi &&
            now-state.active.received_ms<=MitsubishiModeHistory::observation_gap_ms;
        if(!continuous || !same_request(sample.reading,state.active.reading)) {
            ++state.request_generation;
            state.request_since_ms=now;
            state.automatic_queued_utc=state.automatic_confirmed_utc=0;
        }
        if(!continuous || sample.reading.power!=state.active.reading.power ||
            sample.reading.mode!=state.active.reading.mode) {
            state.mode_since_ms=now;
            state.dwell.command_dispatched(now);
        }
        state.dwell.observe(sample.reading.power,now,true);
    }
    state.active=std::move(sample);
}
void install(Snapshot sample) {
    state.saved=sample.config;
    state.active={};
    state.limits={};
    state.approved_device={};
    state.approved_model[0]=0;
    state.requested={};state.sent={};state.confirmed={};
    state.automatic_queued_utc=state.automatic_confirmed_utc=0;
    configuration_revision.store(++state.revision);
    cottage_daikin_automatic_cancel();
    observe(std::move(sample));
}
void enqueue_command(Command command) {
    command.queued_utc=time(nullptr);
    state.pending={Task::Write,state.active.link,{},false,std::move(command)};
    state.requested={state.pending.command.patch,state.pending.command.queued_utc,true};
    state.sent={};
    state.status="write_queued";
}
bool command_current(const Work& work) {
    const auto& cmd=work.command;
    if(!work.link.current() || cmd.revision!=configuration_revision.load() ||
       !cottage_daikin_selected() || cottage_ota_network_maintenance())return false;
    return cmd.automatic?
        cottage_daikin_automatic_write_current(cmd.authorization,cmd.config,cmd.settings,cmd.proposal):
        !cottage_daikin_automatic_enabled();
}
void mark_dispatched(const Work& work,bool mode_change) {
    xSemaphoreTake(gate,portMAX_DELAY);
    if(work.link.current() && work.command.revision==configuration_revision.load()) {
        state.sent={work.command.patch,time(nullptr),true};
        if(mode_change)state.dwell.command_dispatched(millis());
    }
    xSemaphoreGive(gate);
}
std::optional<std::string> get(const char* address,std::string_view path,const Link& link,
                               const Work* command=nullptr,bool mode_change=false) {
    IPAddress ip;
    if(!link.current() || !local_host(address,ip))return {};
    const std::string url="http://"+std::string(address)+std::string(path);
    WiFiClient client;
    HTTPClient req;
    const char* headers[]={"Transfer-Encoding"};
    req.collectHeaders(headers,1);
    req.setReuse(false);
    req.setConnectTimeout(1100);
    req.setTimeout(1300);
    req.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!req.begin(client,url.c_str()))return {};
    req.addHeader("Cache-Control","no-cache");
    if(command) {
        if(!command_current(*command))return {};
        mark_dispatched(*command,mode_change);
        if(!command_current(*command)) {
            xSemaphoreTake(gate,portMAX_DELAY);
            state.sent={};
            xSemaphoreGive(gate);
            return {};
        }
    }
    const int status=req.GET(),len=req.getSize();
    std::optional<std::string> result;
    if(status==200 && len!=0 && len<=static_cast<int>(max_body) &&
       req.header("Transfer-Encoding").length()==0) {
        std::string body;
        auto& stream=req.getStream();
        std::array<uint8_t,128> bytes{};
        const uint32_t start=millis();
        bool complete=false;
        while(link.current() && millis()-start<1300) {
            if(len>=0 && body.size()==static_cast<size_t>(len)) {complete=true;break;}
            const int available=stream.available();
            if(available>0) {
                const auto n=static_cast<size_t>(available);
                const size_t residual=(len>=0?static_cast<size_t>(len):max_body)-body.size();
                if(n>residual)break;
                const int read=stream.read(bytes.data(),std::min(n,bytes.size()));
                if(read>0)body.append(reinterpret_cast<const char*>(bytes.data()),read);
                else vTaskDelay(pdMS_TO_TICKS(15));
            } else if(!stream.connected()) {
                complete=len<0;
                break;
            } else vTaskDelay(pdMS_TO_TICKS(15));
        }
        if(complete && !body.empty() && link.current())result=std::move(body);
    }
    req.end();
    return result;
}
struct Poll {
    Snapshot sample{};
    const char* status="connection_failed";
};
Poll poll(const Config& config,const Link& link) {
    Poll result;
    const auto basic=get(config.host,identity_path,link);
    if(!basic)return result;
    const auto device=decode_identity(*basic);
    if(!device) {result.status="unsupported";return result;}
    if(config.mac[0] && device->mac!=config.mac) {result.status="device_changed";return result;}
    const auto control=get(config.host,state_path,link);
    if(!control) {result.status="no_legacy_control";return result;}
    const auto reading=decode_state(*control);
    if(!reading) {result.status="no_legacy_control";return result;}
    auto& out=result.sample;
    out.config=config;out.config.schema=config_schema;
    strlcpy(out.config.mac,device->mac.c_str(),sizeof(out.config.mac));
    out.device=*device;
    out.reading=*reading;out.link=link;
    out.received_ms=millis();out.received_utc=time(nullptr);
    if(const auto reply=get(config.host,sensor_path,link))
        if(const auto values=decode_sensors(*reply))out.temperatures=*values;
    out.received=link.current();
    result.status=out.received?"ok":"link_changed";
    return result;
}
struct WriteResult {
    const char* status="write_cancelled";
    Snapshot sample{};
    bool observed=false,confirmed=false;
};
WriteResult write_command(const Work& work,const Config& config,const Device& device,
                           const PumpHeatingLimits& limits) {
    WriteResult result;
    if(!command_current(work))return result;
    auto current=poll(config,work.link);
    result.observed=true;
    if(!current.sample.received) {result.status=current.status;return result;}
    result.sample=std::move(current.sample);
    if(!same_device(result.sample.device,device)) {
        result.status="device_changed";
        return result;
    }
    const auto& cmd=work.command;
    const auto before=result.sample.reading;
    xSemaphoreTake(gate,portMAX_DELAY);
    observe(result.sample);
    const bool mode_allowed=!changes_mode(before,cmd.patch) ||
        state.dwell.since_change(millis())>=mitsubishi_min_mode_dwell_ms;
    xSemaphoreGive(gate);
    if((cmd.automatic || (!cmd.patch.power && !cmd.patch.heat_mode)) &&
       !same_request(before,cmd.previous)) {
        result.status="external_change";return result;
    }
    if(readback_matches(before,before,cmd.patch)) {result.status="already_applied";return result;}
    const auto path=set_request(before,cmd.patch,limits);
    if(!path) {result.status="unsupported_command";return result;}
    if(!mode_allowed) {result.status="mode_dwell";return result;}
    if(!command_current(work))return result;
    const auto ack=get(config.host,*path,work.link,&work,changes_mode(before,cmd.patch));
    result.status=ack && accepted(*ack)?"unconfirmed":"send_failed";
    result.sample={};
    // Read after dispatch even when the response timed out. Never resend a SET.
    for(unsigned attempt=0;attempt<3 && work.link.current();++attempt) {
        vTaskDelay(pdMS_TO_TICKS(350));
        auto after=poll(config,work.link);
        if(!after.sample.received)continue;
        result.sample=std::move(after.sample);
        if(!same_device(result.sample.device,device)) {result.status="device_changed";break;}
        if(readback_matches(before,result.sample.reading,cmd.patch) && command_current(work)) {
            xSemaphoreTake(gate,portMAX_DELAY);
            result.confirmed=state.sent.present;
            xSemaphoreGive(gate);
            if(result.confirmed)result.status="confirmed";
            break;
        }
    }
    return result;
}
void add_receipt(cJSON* root,const char* name,const Receipt& receipt) {
    if(!receipt.present)return;
    auto* value=cJSON_AddObjectToObject(root,name);
    if(!value)return;
    cJSON_AddNumberToObject(value,"utc",receipt.utc);
    if(receipt.patch.power)cJSON_AddBoolToObject(value,"power",*receipt.patch.power);
    if(receipt.patch.heat_target_c)cJSON_AddNumberToObject(value,"target_c",*receipt.patch.heat_target_c);
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
    const bool pilot=!draft && cottage_daikin_selected() && approved();
    const bool write_available=pilot && available() && !cottage_daikin_automatic_enabled();
    const auto limits=state.limits;
    const auto requested=state.requested,sent=state.sent,confirmed=state.confirmed;
    const bool receipts_current=state.approval_link.current() &&
        (!state.active.received || same_device(state.approved_device,state.active.device));
    const String approved_model=state.approved_model;
    const auto revision=configuration_revision.load();
    xSemaphoreGive(gate);
    auto* root=cJSON_CreateObject();
    if(!root) {web.send(503,"application/json","{}");return;}
    const bool current=fresh(sample);
    cJSON_AddBoolToObject(root,"configured",configured(config));
    cJSON_AddBoolToObject(root,"fresh",current);
    cJSON_AddBoolToObject(root,"busy",busy);
    if(staged)cJSON_AddNumberToObject(root,"draft_id",id);
    cJSON_AddStringToObject(root,"result",result);
    if(!draft) {
        cJSON_AddNumberToObject(root,"revision",revision);
        cJSON_AddBoolToObject(root,"pilot_approved",pilot);
        cJSON_AddBoolToObject(root,"write_available",write_available);
        if(pilot) {
            cJSON_AddStringToObject(root,"pilot_model",approved_model.c_str());
            cJSON_AddNumberToObject(root,"minimum_c",limits.minimum_c);
            cJSON_AddNumberToObject(root,"maximum_c",limits.maximum_c);
            cJSON_AddNumberToObject(root,"step_c",limits.step_c);
        }
        if(cottage_daikin_selected() && receipts_current) {
            add_receipt(root,"requested",requested);
            add_receipt(root,"sent",sent);
            add_receipt(root,"confirmed",confirmed);
        }
    }
    if(configured(config)) {
        cJSON_AddStringToObject(root,"host",config.host);
        cJSON_AddStringToObject(root,"device",config.mac);
    }
    if(current) {
        cJSON_AddStringToObject(root,"firmware",sample.device.firmware.c_str());
        if(!sample.device.model.empty())cJSON_AddStringToObject(root,"model",sample.device.model.c_str());
        cJSON_AddBoolToObject(root,"power",sample.reading.power);
        cJSON_AddNumberToObject(root,"mode",sample.reading.mode);
        if(sample.reading.target_c)cJSON_AddNumberToObject(root,"target_c",*sample.reading.target_c);
        if(sample.temperatures.indoor_c)cJSON_AddNumberToObject(root,"indoor_c",*sample.temperatures.indoor_c);
        if(sample.temperatures.outdoor_c)cJSON_AddNumberToObject(root,"outdoor_c",*sample.temperatures.outdoor_c);
        if(sample.temperatures.compressor_hz)
            cJSON_AddNumberToObject(root,"compressor_hz",*sample.temperatures.compressor_hz);
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
        strlcpy(state.pending.host,host,sizeof(state.pending.host));
    }
    xSemaphoreGive(gate);
    web.send(busy?409:202,"application/json",busy?
        "{\"error\":\"busy\"}":"{\"queued\":true}");
}
}

bool cottage_daikin_selected() {
    return std::strcmp(site_settings_snapshot().pump_adapter,adapter_id)==0;
}
bool cottage_daikin_needs_worker() {
    if(!gate)return false;
    if(cottage_daikin_selected())return true;
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool pending=state.pending.task!=Task::None;
    xSemaphoreGive(gate);
    return pending;
}
void cottage_daikin_register_routes(WebServer& web) {
    gate=xSemaphoreCreateMutex();
    if(!gate)return;
    Preferences p;
    Config saved{};
    if(p.begin("cottage_dai",true) && p.getBytesLength("legacy1")==sizeof(saved) &&
       p.getBytes("legacy1",&saved,sizeof(saved))==sizeof(saved) && configured(saved))
        state.saved=saved;
    web.on("/api/daikin/status",HTTP_GET,[&web](){
        if(web_ready(web,false))status(web);
    });
    web.on("/api/daikin/identify",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        IPAddress address;
        if(!local_host(web.arg("host").c_str(),address)) {
            web.send(400,"application/json","{\"error\":\"invalid_address\"}");return;
        }
        const auto host=address.toString();
        enqueue(web,Task::Identify,host.c_str());
    });
    web.on("/api/daikin/test",HTTP_POST,[&web](){
        if(web_ready(web,true))enqueue(web,Task::Read);
    });
    web.on("/api/daikin/pilot",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const String enabled=web.arg("enabled"),id=web.arg("device"),revision=web.arg("revision");
        const String low=web.arg("min"),high=web.arg("max"),step=web.arg("step"),model=web.arg("model");
        const auto limits=model_limits(low.c_str(),high.c_str(),step.c_str());
        const bool requested=enabled=="true";
        if((!requested && enabled!="false") || (requested && (!limits || model.length()<3 || model.length()>47))) {
            web.send(400,"application/json","{\"error\":\"model_limits\"}");return;
        }
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool allowed=cottage_daikin_selected() && !state.busy && state.pending.task==Task::None &&
            (!requested || (revision==String(configuration_revision.load()) && fresh(state.active) &&
                            id==state.active.device.mac.c_str()));
        if(allowed) {
            cottage_daikin_automatic_cancel();
            state.limits=requested?*limits:PumpHeatingLimits{};
            state.approved_device=requested?state.active.device:Device{};
            strlcpy(state.approved_model,requested?model.c_str():"",sizeof(state.approved_model));
            state.requested={};state.sent={};state.confirmed={};
            state.automatic_queued_utc=state.automatic_confirmed_utc=0;
            state.approval_revision=configuration_revision.load();
            state.approval_link=live_link();
            state.status=requested?"pilot_approved":"ok";
        }
        xSemaphoreGive(gate);
        web.send(allowed?200:409,"application/json",allowed?
            "{\"saved\":true}":"{\"error\":\"pilot_required\"}");
    });
    web.on("/api/daikin/control",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const String action=web.arg("action"),target=web.arg("target"),revision=web.arg("revision");
        Patch patch;
        if(action=="off")patch.power=false;
        else if(action=="heat" || action=="target") {
            patch.heat_target_c=heat_target(target.c_str());
            if(action=="heat") {patch.power=true;patch.heat_mode=true;}
        }
        if((!patch.power && !patch.heat_target_c) || (patch.heat_mode && !patch.heat_target_c)) {
            web.send(400,"application/json","{\"error\":\"unsupported_command\"}");return;
        }
        xSemaphoreTake(gate,portMAX_DELAY);
        const auto& current=state.active.reading;
        const bool ready=cottage_daikin_selected() && !cottage_daikin_automatic_enabled() &&
            revision==String(configuration_revision.load()) && available();
        const bool supported=ready && set_request(current,patch,state.limits).has_value();
        const bool allowed=supported && (!changes_mode(current,patch) ||
            state.dwell.since_change(millis())>=mitsubishi_min_mode_dwell_ms);
        if(allowed) {
            Command command{};
            command.patch=patch;command.previous=current;
            command.revision=configuration_revision.load();
            enqueue_command(std::move(command));
        }
        xSemaphoreGive(gate);
        web.send(allowed?202:409,"application/json",allowed?"{\"queued\":true}":
            !ready?"{\"error\":\"pilot_required\"}":!supported?"{\"error\":\"unsupported_command\"}":
            "{\"error\":\"mode_dwell\"}");
    });
    web.on("/api/daikin/commit",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const String id=web.arg("draft_id");
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool matching=state.stage_id && id==String(state.stage_id) && fresh(state.staged) &&
            !state.busy && state.pending.task==Task::None;
        const bool saved=matching && persists(state.staged.config);
        if(saved) {
            install(std::move(state.staged));
            state.staged={};state.stage_id=0;
            state.status="ok";
        }
        xSemaphoreGive(gate);
        web.send(saved?200:matching?500:409,"application/json",saved?
            "{\"saved\":true}":matching?"{\"error\":\"storage\"}":"{\"error\":\"connection_failed\"}");
    });
}

uint32_t cottage_daikin_configuration_generation() {return configuration_revision.load();}
uint32_t cottage_daikin_calibration_identity() {
    if(!gate)return 0;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto saved=state.saved;
    xSemaphoreGive(gate);
    if(!configured(saved))return 0;
    uint32_t hash=2166136261u;
    for(const char digit:saved.mac) {hash^=static_cast<uint8_t>(digit);hash*=16777619u;}
    return hash;
}
bool cottage_daikin_automatic_ready() {
    if(!gate || !cottage_daikin_selected())return false;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto mode=room_mode(state.active.reading);
    const bool ready=available() && (mode==PumpRoomMode::Off || mode==PumpRoomMode::Heat);
    xSemaphoreGive(gate);
    return ready;
}
ControlBlock cottage_daikin_submit_automatic(const PumpRoomState& earlier,ControlSnapshot proposal,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    if(!gate || !cottage_daikin_automatic_enabled())return ControlBlock::Disabled;
    xSemaphoreTake(gate,portMAX_DELAY);
    ControlBlock result=ControlBlock::InputsUnverified;
    if(available() && earlier.configuration_generation==configuration_revision.load() &&
       earlier.endpoint_generation==state.active.link.endpoint &&
       earlier.link_generation==state.active.link.wifi &&
       earlier.request_generation==state.request_generation) {
        const auto& reading=state.active.reading;
        const auto mode=room_mode(reading);
        if(mode==PumpRoomMode::Off || mode==PumpRoomMode::Heat) {
            const auto initial=proposal.proposal;
            if(initial.action==BootstrapAction::HeatSetpoint && std::isfinite(initial.heat_setpoint_c)) {
                const double step=state.limits.step_c;
                proposal.proposal.heat_setpoint_c=std::clamp(std::round(initial.heat_setpoint_c/step)*step,
                    state.limits.minimum_c,state.limits.maximum_c);
            }
            proposal.current_heat=mode==PumpRoomMode::Heat;
            proposal.current_setpoint_c=reading.target_c.value_or(NAN);
            proposal.cn105_link_fresh=proposal.bridge_get_ok=true;
            proposal.ms_since_mode_change=state.dwell.since_change(millis());
            const auto decision=choose_pump_control(proposal,state.limits,HeatTransition::Combined);
            result=decision.gate;
            if(decision.step!=ControlStep::None) {
                Command cmd{};
                cmd.previous=reading;cmd.proposal=initial;cmd.automatic=true;
                cmd.revision=configuration_revision.load();
                cmd.authorization=authorization;cmd.config=config;cmd.settings=settings;
                if(decision.step==ControlStep::SetOff)cmd.patch.power=false;
                else {
                    cmd.patch.heat_target_c=decision.target_c;
                    if(decision.step==ControlStep::SetHeat) {
                        cmd.patch.power=true;cmd.patch.heat_mode=true;
                    }
                }
                enqueue_command(std::move(cmd));
            }
        }
    }
    xSemaphoreGive(gate);
    return result;
}
enevina::PumpRoomState cottage_daikin_room_state() {
    using namespace enevina;
    PumpRoomState result{};
    if(!gate || !cottage_daikin_selected())return result;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto snapshot=state.active;
    result.configuration_generation=configuration_revision.load();
    if(approved())result.heating=state.limits;
    result.request_generation=state.request_generation;
    result.request_minutes=(millis()-state.request_since_ms)/60000;
    result.mode_minutes=(millis()-state.mode_since_ms)/60000;
    result.automatic_queued_utc=state.automatic_queued_utc;
    result.automatic_confirmed_utc=state.automatic_confirmed_utc;
    xSemaphoreGive(gate);
    if(!fresh(snapshot))return result;
    result.source=PumpStateSource::DirectLan;
    result.protocol=PumpRoomProtocol::DaikinLegacy;
    result.type=HeatPumpType::AirToAir;
    result.endpoint_generation=snapshot.link.endpoint;result.link_generation=snapshot.link.wifi;
    result.mode=room_mode(snapshot.reading);
    if(snapshot.reading.target_c)result.setpoint_c=*snapshot.reading.target_c;
    if(snapshot.temperatures.indoor_c)result.indoor_c=*snapshot.temperatures.indoor_c;
    if(snapshot.temperatures.outdoor_c)result.outdoor_c=*snapshot.temperatures.outdoor_c;
    if(snapshot.received_utc>=1700000000)result.evidence={snapshot.received_utc,0,false};
    return result;
}

void cottage_daikin_tick() {
    static uint32_t last=0,last_endpoint=0,last_wifi=0;
    if(WiFi.status()!=WL_CONNECTED || cottage_ota_network_maintenance()) {
        return;
    }
    const auto link=live_link();
    const bool selected=cottage_daikin_selected();
    xSemaphoreTake(gate,portMAX_DELAY);
    auto job=state.pending;
    if(job.task==Task::None && selected && configured(state.saved) &&
       (millis()-last>=read_interval_ms || link.endpoint!=last_endpoint || link.wifi!=last_wifi))
        job={Task::Read,link,{},false};
    const bool staged=job.draft && state.stage_id && state.staged.link.current();
    Config candidate=staged?state.staged.config:state.saved;
    const auto approval=state.approved_device;
    const auto limits=state.limits;
    state.pending={};
    state.busy=job.task!=Task::None;
    xSemaphoreGive(gate);
    if(job.task==Task::None) {return;}
    if(job.task==Task::Identify) {
        candidate={};
        strlcpy(candidate.host,job.host,sizeof(candidate.host));
    }
    Poll result{};
    WriteResult command{};
    if(job.link.current() && candidate.host[0]) {
        enevina_firmware::NetworkBudgetLease lease(700,72*1024,16*1024);
        if(lease.held()) {
            if(job.task==Task::Write)command=write_command(job,candidate,approval,limits);
            else result=poll(candidate,job.link);
        } else {
            result.status="network_busy";
            command.status="network_busy";
        }
    }
    xSemaphoreTake(gate,portMAX_DELAY);
    state.busy=false;
    if(job.link.current()) {
        if(job.task==Task::Write) {
            result.status=command.status;
            if(cottage_daikin_selected() && job.command.revision==configuration_revision.load()) {
                if(command.observed)observe(std::move(command.sample));
                if(command.confirmed) {
                    state.confirmed={job.command.patch,state.active.received_utc,true};
                    if(job.command.automatic) {
                        state.automatic_queued_utc=job.command.queued_utc;
                        state.automatic_confirmed_utc=state.active.received_utc;
                    }
                }
            }
        } else if(job.task==Task::Identify && result.sample.received) {
            if(job.draft) {
                if(++state.next_id==0)++state.next_id;
                state.staged=std::move(result.sample);state.stage_id=state.next_id;
            } else if(persists(result.sample.config)) {
                install(std::move(result.sample));
                state.staged={};state.stage_id=0;
            } else result.status="storage";
        } else if(job.task==Task::Read) {
            // A failed active poll withdraws the previous reading. A
            // staged read retains the proposed address for explicit retry.
            if(staged) {
                if(result.sample.received)state.staged=std::move(result.sample);
                else state.staged.received=false;
            } else observe(std::move(result.sample));
        }
    } else result.status="link_changed";
    (job.draft?state.draft_status:state.status)=result.status;
    xSemaphoreGive(gate);
    last=millis();last_endpoint=job.link.endpoint;last_wifi=job.link.wifi;
}
#endif
