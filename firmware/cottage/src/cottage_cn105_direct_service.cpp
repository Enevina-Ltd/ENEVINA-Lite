#ifndef COTTAGE_BENCH_MODE
#include "cottage_cn105_direct_service.hpp"
#include "cottage_air_service.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "pump/cn105_uart.hpp"

#include <HardwareSerial.h>
#include <esp_system.h>
#include <algorithm>
#include <cstdlib>
#include <Preferences.h>
#include <WebServer.h>
#include <cJSON.h>
#include <atomic>
#include <cstring>
#include <ctime>

namespace {
using namespace enevina;
using namespace enevina::cottage;
using namespace enevina::cn105_uart;
constexpr char adapter_id[]="mitsubishi_cn105_direct";
constexpr int uart_rx=18,uart_tx=17;
constexpr uint32_t poll_interval=15000,valid_age=60000,response_timeout=1100;

struct Link {
    uint32_t endpoint=0,wifi=0;
    bool current() const {
        return endpoint==site_settings_endpoint_generation() &&
            wifi==cottage_wifi_link_generation();
    }
};
struct Sample {
    Settings climate{};
    Room temperature{};
    Operation operating{};
    Link link{};
    uint32_t baud=0,bus_generation=0,received_ms=0;
    int64_t received_utc=0;
    bool received=false;
};
struct Command {
    Settings previous{};
    Patch patch{};
    BootstrapDecision proposal{};
    uint32_t revision=0,bus=0,authorization=0,config=0,settings=0;
    int64_t queued_utc=0;
    bool automatic=false;
};
struct Job {
    uint32_t baud=0;
    Link link{};
    bool draft=false;
    std::optional<Command> command;
};
struct Receipt {
    Patch patch{};
    int64_t utc=0;
};
struct PairRecord {
    uint32_t baud=0,enrollment=0;
};
struct State {
    PairRecord pair{};
    uint32_t draft_id=0,next_id=0,request_generation=0;
    Sample active{},staged{};
    Job job{};
    bool busy=false;
    uint32_t request_since=0,mode_since=0;
    const char* status="not_tested";
    const char* staged_status="not_tested";
    PumpHeatingLimits limits{};
    uint32_t approved_bus=0,approved_revision=0;
    Link approved_link{};
    char approved_model[48]{};
    MitsubishiModeHistory dwell{};
    Receipt requested{},sent{},confirmed{};
    int64_t automatic_queued_utc=0,automatic_confirmed_utc=0;
};
HardwareSerial uart(1);
SemaphoreHandle_t gate=nullptr;
State state{};
std::atomic<bool> uart_active{false};
uint32_t active_baud=0;
std::atomic<uint32_t> bus_generation{0},revision{0};
Link active_link{};

bool valid_baud(uint32_t baud) {return baud==2400 || baud==9600;}
Link live_link() {return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};}
bool fresh(const Sample& sample) {
    return sample.received && sample.link.current() && millis()-sample.received_ms<=valid_age;
}
std::optional<PairRecord> store_pair(uint32_t baud) {
    if(!valid_baud(baud))return {};
    Preferences p;
    const PairRecord record{baud,esp_random()|1u};
    PairRecord check{};
    if(!p.begin("cottage_cn1",false) || p.putBytes("pair1",&record,sizeof(record))!=sizeof(record) ||
       p.getBytes("pair1",&check,sizeof(check))!=sizeof(check) ||
       check.baud!=record.baud || check.enrollment!=record.enrollment)return {};
    return record;
}
bool active_fresh(const Sample& sample) {
    return fresh(sample) && sample.bus_generation==bus_generation.load();
}
bool approved() {
    return state.limits.heating && state.approved_bus==bus_generation.load() &&
        state.approved_revision==revision.load() && state.approved_link.current() &&
        state.active.climate.encoding!=TemperatureEncoding::Unknown &&
        state.limits.step_c==(state.active.climate.encoding==TemperatureEncoding::HalfDegree?.5:1.);
}
bool can_queue() {
    return approved() && active_fresh(state.active) && !state.busy && state.job.baud==0;
}
bool same_settings(const Settings& a,const Settings& b) {
    return a.power==b.power && a.mode==b.mode && a.target_c==b.target_c && a.encoding==b.encoding;
}
bool changes_mode(const Settings& a,const Patch& b) {
    return (b.power && a.power!=b.power) || (b.heat && a.mode!=Mode::Heat);
}
bool authorized(const Job& job) {
    const auto& command=*job.command;
    if(!job.link.current() || !cottage_cn105_direct_selected() ||
       command.revision!=revision.load() || command.bus!=bus_generation.load() ||
       cottage_ota_network_maintenance())return false;
    return command.automatic?
        cottage_cn105_direct_automatic_write_current(command.authorization,command.config,
                                                      command.settings,command.proposal):
        !cottage_cn105_direct_automatic_enabled();
}
void queue(Command command) {
    command.queued_utc=time(nullptr);
    state.job={state.pair.baud,state.active.link,false,command};
    state.requested={command.patch,command.queued_utc};
    state.sent={};
    state.status="write_queued";
}
std::optional<double> parse_target_c(const String& argument) {
    const auto* first=argument.c_str();
    char* end=nullptr;
    const double c=std::strtod(first,&end);
    if(end==first || *end || c<16 || c>31 || !std::isfinite(c))return {};
    return c;
}
void disconnect() {
    if(uart_active.exchange(false))uart.end();
    active_baud=0;
    ++bus_generation;
}
bool ready_for_frame() {
    // Drain replies to previous transactions before sending the next request.
    for(unsigned i=0;i<128 && uart.available();++i)uart.read();
    return uart.available()==0;
}
bool send_frame(const Frame& request) {
    if(uart.write(request.bytes.data(),request.size)!=request.size)return false;
    uart.flush(true);
    return true;
}
template<typename Accept>
std::optional<Frame> exchange(const Frame& request,const Link& link,Accept accept) {
    if(!link.current() || !ready_for_frame() || !send_frame(request))return {};
    FrameReader reader;
    const uint32_t start=millis();
    while(link.current() && millis()-start<response_timeout) {
        for(unsigned i=0;i<40 && uart.available();++i) {
            const int next=uart.read();
            if(next<0)break;
            if(const auto result=reader.append(static_cast<uint8_t>(next)))
                if(accept(*result))return result;
        }
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    return {};
}
template<typename Decode>
auto read(Query tag,const Link& link,Decode decode) {
    const auto incoming=exchange(query(tag),link,[&](const Frame& frame){
        return response(frame,command_data_reply) && frame.bytes[4]==max_payload &&
            frame.bytes[5]==static_cast<uint8_t>(tag);
    });
    return incoming?decode(*incoming):decltype(decode(*incoming)){};
}
Sample poll(uint32_t baud,const Link& link) {
    if(!valid_baud(baud) || !link.current())return {};
    if(!uart_active.load() || baud!=active_baud ||
       active_link.endpoint!=link.endpoint || active_link.wifi!=link.wifi) {
        disconnect();
        uart.begin(baud,SERIAL_8E1,uart_rx,uart_tx);
        uart_active.store(true);
        active_baud=baud;active_link=link;
        if(!exchange(connect(),link,[](const Frame& frame){return response(frame,command_connect_reply);})) {
            disconnect();
            return {};
        }
    }
    const auto climate=read(Query::Settings,link,settings);
    if(!climate || !climate->power || climate->mode==Mode::Unknown) {
        disconnect();
        return {};
    }
    Sample out;
    out.climate=*climate;
    out.received_utc=time(nullptr);
    if(const auto temperatures=read(Query::Room,link,room))out.temperature=*temperatures;
    if(const auto compressor=read(Query::Operating,link,operation))out.operating=*compressor;
    out.link=link;
    out.baud=baud;out.bus_generation=bus_generation.load();
    out.received_ms=millis();
    out.received=link.current();
    return out;
}
void observe(const Sample& sample) {
    if(sample.received) {
        const bool continuous=fresh(state.active) && state.active.bus_generation==sample.bus_generation;
        const auto& previous=state.active.climate;
        const bool same_mode=sample.climate.power==previous.power && sample.climate.mode==previous.mode;
        if(!continuous || !same_mode) {
            state.mode_since=sample.received_ms;
            state.dwell.command_dispatched(sample.received_ms);
        }
        if(sample.climate.power)state.dwell.observe(*sample.climate.power,sample.received_ms,true);
        if(!continuous || !same_mode || sample.climate.target_c!=previous.target_c ||
           sample.climate.encoding!=previous.encoding) {
            state.request_since=sample.received_ms;
            ++state.request_generation;
            state.automatic_queued_utc=state.automatic_confirmed_utc=0;
        }
    }
    state.active=sample;
}
void activate(const PairRecord& pair,const Sample& sample) {
    state.pair=pair;state.active={};
    observe(sample);
    state.staged={};state.draft_id=0;
    state.limits={};state.approved_model[0]=0;
    state.requested={};state.sent={};state.confirmed={};
    revision.fetch_add(1);
    cottage_cn105_direct_automatic_cancel();
    state.status="ok";
}
struct WriteResult {
    const char* status="write_cancelled";
    Sample reading{};
    bool observed=false,confirmed=false;
};
WriteResult perform_write(const Job& job,const PumpHeatingLimits& limits) {
    WriteResult result;
    if(!authorized(job))return result;
    const auto& command=*job.command;
    const auto before=poll(job.baud,job.link);
    result.reading=before;
    result.observed=true;
    if(!before.received) {result.status="connection_failed";return result;}
    xSemaphoreTake(gate,portMAX_DELAY);
    observe(before);
    const bool mode_change=changes_mode(before.climate,command.patch);
    const bool allowed=!mode_change || state.dwell.since_change(millis())>=mitsubishi_min_mode_dwell_ms;
    const bool permit=approved();
    xSemaphoreGive(gate);
    if((command.automatic || (!command.patch.power && !command.patch.heat)) &&
       !same_settings(before.climate,command.previous)) {
        result.status="external_change";return result;
    }
    if(readback_matches(before.climate,before.climate,command.patch)) {
        result.status="already_applied";return result;
    }
    if(!allowed) {result.status="mode_dwell";return result;}
    const auto frame=heat_command(before.climate,command.patch,limits);
    if(!frame || !permit) {result.status="unsupported_command";return result;}
    if(!ready_for_frame() || !authorized(job))return result;
    if(!send_frame(*frame)) {result.status="send_failed";return result;}
    xSemaphoreTake(gate,portMAX_DELAY);
    if(job.link.current() && command.bus==bus_generation.load()) {
        state.sent={command.patch,time(nullptr)};
        if(mode_change)state.dwell.command_dispatched(millis());
    }
    xSemaphoreGive(gate);

    result.status="unconfirmed";
    result.reading={};
    // The write acknowledgement contains no resulting settings. Query the bus again.
    for(unsigned attempt=0;attempt<3 && job.link.current();++attempt) {
        vTaskDelay(pdMS_TO_TICKS(350));
        const auto after=poll(job.baud,job.link);
        if(!after.received)continue;
        result.reading=after;
        if(after.bus_generation==command.bus &&
           readback_matches(before.climate,after.climate,command.patch) && authorized(job)) {
            result.confirmed=true;result.status="confirmed";break;
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
bool access(WebServer& web,bool mutation) {
    if(!cottage_wifi_require_home_lan(web))return false;
    if(!gate) {web.send(503,"application/json","{}");return false;}
    if(mutation && (!web.hasArg("token") ||
       !enevina::cottage::c23_authorize_local_setting(web.arg("token").c_str()))) {
        web.send(403,"application/json","{\"error\":\"authorization\"}");return false;
    }
    return true;
}
void status(WebServer& web) {
    const bool draft=web.arg("draft")=="true";
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool staged=draft && state.draft_id && state.staged.link.current();
    const auto sample=staged?state.staged:state.active;
    const auto baud=staged?sample.baud:state.pair.baud;
    const auto id=staged?state.draft_id:0;
    const auto result=draft?state.staged_status:state.status;
    const bool busy=state.busy || state.job.baud!=0;
    const bool pilot=!draft && cottage_cn105_direct_selected() && approved();
    const bool writable=pilot && can_queue() && !cottage_cn105_direct_automatic_enabled();
    const auto limits=state.limits;
    const String model=state.approved_model;
    const auto requested=state.requested,sent=state.sent,confirmed=state.confirmed;
    const auto generation=revision.load();
    xSemaphoreGive(gate);

    auto* root=cJSON_CreateObject();
    if(!root) {web.send(503,"application/json","{}");return;}
    const bool current=fresh(sample) && (staged || sample.bus_generation==bus_generation.load());
    cJSON_AddBoolToObject(root,"configured",valid_baud(baud));
    cJSON_AddBoolToObject(root,"fresh",current);
    cJSON_AddBoolToObject(root,"busy",busy);
    cJSON_AddStringToObject(root,"result",result);
    if(id)cJSON_AddNumberToObject(root,"draft_id",id);
    if(!draft) {
        cJSON_AddNumberToObject(root,"revision",generation);
        cJSON_AddBoolToObject(root,"pilot_approved",pilot);
        cJSON_AddBoolToObject(root,"write_available",writable);
        if(pilot) {
            cJSON_AddStringToObject(root,"pilot_model",model.c_str());
            cJSON_AddNumberToObject(root,"minimum_c",limits.minimum_c);
            cJSON_AddNumberToObject(root,"maximum_c",limits.maximum_c);
            cJSON_AddNumberToObject(root,"step_c",limits.step_c);
            receipt(root,"requested",requested);receipt(root,"sent",sent);receipt(root,"confirmed",confirmed);
        }
    }
    if(valid_baud(baud))cJSON_AddNumberToObject(root,"baud",baud);
    if(current) {
        if(sample.climate.power)cJSON_AddBoolToObject(root,"power",*sample.climate.power);
        if(sample.climate.mode!=Mode::Unknown)
            cJSON_AddNumberToObject(root,"mode",static_cast<unsigned>(sample.climate.mode));
        if(sample.climate.target_c)cJSON_AddNumberToObject(root,"target_c",*sample.climate.target_c);
        if(sample.climate.encoding!=TemperatureEncoding::Unknown)
            cJSON_AddNumberToObject(root,"observed_step_c",sample.climate.encoding==TemperatureEncoding::HalfDegree?.5:1.);
        if(sample.temperature.indoor_c)cJSON_AddNumberToObject(root,"indoor_c",*sample.temperature.indoor_c);
        if(sample.temperature.outdoor_c)cJSON_AddNumberToObject(root,"outdoor_c",*sample.temperature.outdoor_c);
        if(sample.operating.compressor_hz)
            cJSON_AddNumberToObject(root,"compressor_hz",*sample.operating.compressor_hz);
    }
    char* payload=cJSON_PrintUnformatted(root);
    web.send(payload?200:503,"application/json",payload?payload:"{}");
    cJSON_free(payload);cJSON_Delete(root);
}
}
bool cottage_cn105_direct_selected() {
    return !std::strcmp(site_settings_snapshot().pump_adapter,adapter_id);
}
bool cottage_cn105_direct_needs_worker() {
    if(!gate)return false;
    if(cottage_cn105_direct_selected() || uart_active.load())return true;
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool pending=state.job.baud!=0;
    xSemaphoreGive(gate);
    return pending;
}
void cottage_cn105_direct_register_routes(WebServer& web) {
    gate=xSemaphoreCreateMutex();
    if(!gate)return;
    Preferences prefs;
    if(prefs.begin("cottage_cn1",true)) {
        PairRecord pair{};
        if(prefs.getBytesLength("pair1")==sizeof(pair) &&
           prefs.getBytes("pair1",&pair,sizeof(pair))==sizeof(pair) && valid_baud(pair.baud))
            state.pair=pair;
        else if(const auto legacy=prefs.getUInt("baud");valid_baud(legacy))state.pair.baud=legacy;
    }
    web.on("/api/cn105-direct/status",HTTP_GET,[&web](){
        if(access(web,false))status(web);
    });
    web.on("/api/cn105-direct/test",HTTP_POST,[&web](){
        if(!access(web,true))return;
        const String value=web.arg("baud");
        const uint32_t baud=value=="2400"?2400:value=="9600"?9600:0;
        if(!baud) {web.send(400,"application/json","{\"error\":\"unsupported\"}");return;}
        const bool draft=web.arg("draft")=="true";
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool busy=state.busy || state.job.baud!=0;
        if(!busy) {
            if(draft) {state.staged={};state.draft_id=0;}
            state.job={baud,live_link(),draft};
        }
        xSemaphoreGive(gate);
        web.send(busy?409:202,"application/json",busy?"{\"error\":\"busy\"}":"{\"queued\":true}");
    });
    web.on("/api/cn105-direct/pilot",HTTP_POST,[&web](){
        if(!access(web,true))return;
        const String enable=web.arg("enabled"),model=web.arg("model"),expected=web.arg("revision");
        const auto low=parse_target_c(web.arg("min")),high=parse_target_c(web.arg("max"));
        const bool requested=enable=="true";
        if((!requested && enable!="false") ||
           (requested && (!low || !high || *low>=*high || model.length()<3 || model.length()>47))) {
            web.send(400,"application/json","{\"error\":\"model_limits\"}");return;
        }
        xSemaphoreTake(gate,portMAX_DELAY);
        const auto encoding=state.active.climate.encoding;
        PumpHeatingLimits limits{};
        if(requested && encoding!=TemperatureEncoding::Unknown)
            limits={*low,*high,encoding==TemperatureEncoding::HalfDegree?.5:1.,true,true};
        const bool allowed=cottage_cn105_direct_selected() && !state.busy && state.job.baud==0 &&
            (!requested || (expected==String(revision.load()) && active_fresh(state.active) &&
                            limits.accepts(*low) && limits.accepts(*high)));
        if(allowed) {
            cottage_cn105_direct_automatic_cancel();
            state.limits=limits;
            state.approved_link=state.active.link;
            state.approved_bus=state.active.bus_generation;
            state.approved_revision=revision.load();
            strlcpy(state.approved_model,requested?model.c_str():"",sizeof(state.approved_model));
            state.requested={};state.sent={};state.confirmed={};
            state.automatic_queued_utc=state.automatic_confirmed_utc=0;
            state.status=requested?"pilot_approved":"ok";
        }
        xSemaphoreGive(gate);
        web.send(allowed?200:409,"application/json",allowed?
            "{\"saved\":true}":"{\"error\":\"pilot_required\"}");
    });
    web.on("/api/cn105-direct/control",HTTP_POST,[&web](){
        if(!access(web,true))return;
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
        xSemaphoreTake(gate,portMAX_DELAY);
        const auto& reading=state.active.climate;
        const bool ready=cottage_cn105_direct_selected() && !cottage_cn105_direct_automatic_enabled() &&
            expected==String(revision.load()) && can_queue();
        const bool supported=ready && heat_command(reading,patch,state.limits).has_value();
        const bool allowed=supported && (!changes_mode(reading,patch) ||
            state.dwell.since_change(millis())>=mitsubishi_min_mode_dwell_ms);
        if(allowed) {
            Command cmd{};
            cmd.patch=patch;cmd.previous=reading;cmd.revision=revision.load();
            cmd.bus=state.active.bus_generation;
            queue(cmd);
        }
        xSemaphoreGive(gate);
        web.send(allowed?202:409,"application/json",allowed?"{\"queued\":true}":
            !ready?"{\"error\":\"pilot_required\"}":!supported?"{\"error\":\"unsupported_command\"}":
            "{\"error\":\"mode_dwell\"}");
    });
    web.on("/api/cn105-direct/commit",HTTP_POST,[&web](){
        if(!access(web,true))return;
        const String id=web.arg("draft_id");
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool matched=state.draft_id && id==String(state.draft_id) && fresh(state.staged) &&
            !state.busy && state.job.baud==0;
        const auto saved=matched?store_pair(state.staged.baud):std::nullopt;
        if(saved)activate(*saved,state.staged);
        xSemaphoreGive(gate);
        web.send(saved?200:matched?500:409,"application/json",saved?
            "{\"saved\":true}":matched?"{\"error\":\"storage\"}":"{\"error\":\"connection_failed\"}");
    });
}
uint32_t cottage_cn105_direct_configuration_generation() {return revision.load();}
uint32_t cottage_cn105_direct_bus_generation() {return bus_generation.load();}
bool cottage_cn105_direct_automatic_ready() {
    if(!gate || !cottage_cn105_direct_selected())return false;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto mode=heating_mode(state.active.climate);
    const bool enabled=can_queue() && (mode==PumpRoomMode::Heat || mode==PumpRoomMode::Off);
    xSemaphoreGive(gate);
    return enabled;
}
ControlBlock cottage_cn105_direct_submit_automatic(const PumpRoomState& earlier,ControlSnapshot proposal,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    if(!gate || !cottage_cn105_direct_automatic_enabled())return ControlBlock::Disabled;
    xSemaphoreTake(gate,portMAX_DELAY);
    ControlBlock result=ControlBlock::InputsUnverified;
    if(can_queue() && earlier.configuration_generation==revision.load() &&
       earlier.request_generation==state.request_generation &&
       earlier.link_generation==state.active.bus_generation &&
       earlier.endpoint_generation==state.active.link.endpoint) {
        const auto& reading=state.active.climate;
        const auto mode=heating_mode(reading);
        if(mode==PumpRoomMode::Off || mode==PumpRoomMode::Heat) {
            const auto initial=proposal.proposal;
            const auto limits=state.limits;
            if(initial.action==BootstrapAction::HeatSetpoint && std::isfinite(initial.heat_setpoint_c))
                proposal.proposal.heat_setpoint_c=std::clamp(
                    std::round(initial.heat_setpoint_c/limits.step_c)*limits.step_c,
                    limits.minimum_c,limits.maximum_c);
            proposal.cn105_link_fresh=proposal.bridge_get_ok=true;
            proposal.current_heat=mode==PumpRoomMode::Heat;
            proposal.current_setpoint_c=reading.target_c.value_or(NAN);
            proposal.ms_since_mode_change=state.dwell.since_change(millis());
            const auto decision=choose_pump_control(proposal,limits,HeatTransition::Combined);
            result=decision.gate;
            if(decision.step!=ControlStep::None) {
                Command cmd{};
                cmd.proposal=initial;cmd.previous=reading;cmd.automatic=true;
                cmd.revision=revision.load();cmd.bus=state.active.bus_generation;
                cmd.authorization=authorization;cmd.config=config;cmd.settings=settings;
                if(decision.step==ControlStep::SetOff)cmd.patch.power=false;
                else {
                    cmd.patch.target_c=decision.target_c;
                    if(decision.step==ControlStep::SetHeat) {cmd.patch.power=true;cmd.patch.heat=true;}
                }
                queue(cmd);
            }
        }
    }
    xSemaphoreGive(gate);
    return result;
}
uint32_t cottage_cn105_direct_calibration_identity() {
    if(!gate)return 0;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto id=state.pair.enrollment?state.pair.enrollment:state.pair.baud;
    xSemaphoreGive(gate);
    return id;
}
PumpRoomState cottage_cn105_direct_room_state() {
    PumpRoomState out{};
    if(!gate || !cottage_cn105_direct_selected())return out;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto sample=state.active;
    out.configuration_generation=revision.load();
    out.request_generation=state.request_generation;
    out.request_minutes=(millis()-state.request_since)/60000;
    out.mode_minutes=(millis()-state.mode_since)/60000;
    out.automatic_queued_utc=state.automatic_queued_utc;
    out.automatic_confirmed_utc=state.automatic_confirmed_utc;
    if(approved())out.heating=state.limits;
    xSemaphoreGive(gate);
    if(!fresh(sample) || sample.bus_generation!=bus_generation.load() ||
       sample.received_utc<1700000000)return out;
    out.source=PumpStateSource::VerifiedBus;
    out.protocol=PumpRoomProtocol::Cn105Direct;
    out.type=HeatPumpType::AirToAir;
    out.endpoint_generation=sample.link.endpoint;out.link_generation=sample.bus_generation;
    out.mode=heating_mode(sample.climate);
    if(sample.climate.target_c)out.setpoint_c=*sample.climate.target_c;
    if(sample.temperature.indoor_c)out.indoor_c=*sample.temperature.indoor_c;
    if(sample.temperature.outdoor_c)out.outdoor_c=*sample.temperature.outdoor_c;
    out.evidence={sample.received_utc,sample.received_utc,true};
    return out;
}
void cottage_cn105_direct_tick() {
    static uint32_t last=0,last_endpoint=0,last_wifi=0;
    if(cottage_ota_network_maintenance())return;
    const auto link=live_link();
    const bool selected=cottage_cn105_direct_selected();
    xSemaphoreTake(gate,portMAX_DELAY);
    auto job=state.job;
    const bool explicit_test=job.baud!=0;
    if(!explicit_test && selected && valid_baud(state.pair.baud) &&
       (millis()-last>=poll_interval || link.endpoint!=last_endpoint || link.wifi!=last_wifi))
        job={state.pair.baud,link};
    const auto limits=state.limits;
    state.job={};
    state.busy=job.baud!=0;
    xSemaphoreGive(gate);
    if(!job.baud) {
        if(!selected && uart_active.load())disconnect();
        return;
    }
    const auto outcome=job.command?perform_write(job,limits):WriteResult{};
    const auto sample=job.command?Sample{}:poll(job.baud,job.link);
    if(job.draft && !selected)disconnect();
    xSemaphoreTake(gate,portMAX_DELAY);
    state.busy=false;
    if(job.link.current()) {
        auto& result=job.draft?state.staged_status:state.status;
        result=sample.received?"ok":"connection_failed";
        if(job.command) {
            result=outcome.status;
            if(cottage_cn105_direct_selected() && job.command->revision==revision.load()) {
                if(outcome.observed)observe(outcome.reading);
                if(outcome.confirmed) {
                    state.confirmed={job.command->patch,state.active.received_utc};
                    if(job.command->automatic) {
                        state.automatic_queued_utc=job.command->queued_utc;
                        state.automatic_confirmed_utc=state.active.received_utc;
                    }
                }
            }
        } else if(job.draft) {
            if(sample.received) {
                if(++state.next_id==0)++state.next_id;
                state.draft_id=state.next_id;state.staged=sample;
            }
        } else if(explicit_test && sample.received && state.pair.baud!=job.baud) {
            if(const auto stored=store_pair(job.baud))activate(*stored,sample);
            else result="storage";
        } else if(!explicit_test || !sample.received || job.baud==state.pair.baud)observe(sample);
    }
    xSemaphoreGive(gate);
    last=millis();last_endpoint=job.link.endpoint;last_wifi=job.link.wifi;
}
#endif
