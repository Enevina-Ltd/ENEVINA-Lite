#pragma once
#include "cottage_air_service.hpp"
#include "cottage_pump_uart.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "cottage/mitsubishi_control_policy.hpp"
#include <esp_system.h>
#include <algorithm>
#include <cstdlib>
#include <Preferences.h>
#include <WebServer.h>
#include <cJSON.h>
#include <atomic>
#include <cstring>
#include <ctime>
#include <optional>

namespace enevina::cottage {
template<typename Protocol>
class SerialHeatingService {
    using Settings=typename Protocol::Settings;
    using Patch=typename Protocol::Patch;
    static constexpr uint32_t poll_interval=15000,valid_age=60000;
    struct Link {
        uint32_t endpoint=0,wifi=0;
        bool current() const {
            return endpoint==site_settings_endpoint_generation() &&
                wifi==cottage_wifi_link_generation();
        }
    };
    struct Sample {
        Settings climate{};
        struct {
            std::optional<double> indoor_c,outdoor_c;
        } temperature;
        std::optional<unsigned> compressor_hz;
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
    inline static SemaphoreHandle_t gate=nullptr;
    inline static State state{};
    inline static uint32_t active_baud=0;
    inline static std::atomic<uint32_t> revision{0};
    inline static Link active_link{};

    static Link live_link() {return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};}
    static bool fresh(const Sample& sample) {
        return sample.received && sample.link.current() && millis()-sample.received_ms<=valid_age;
    }
    static std::optional<PairRecord> store_pair(uint32_t baud) {
        if(!Protocol::valid_baud(baud))return {};
        Preferences p;
        const PairRecord record{baud,esp_random()|1u};
        PairRecord check{};
        if(!p.begin(Protocol::nvs,false) || p.putBytes("pair1",&record,sizeof(record))!=sizeof(record) ||
           p.getBytes("pair1",&check,sizeof(check))!=sizeof(check) ||
           check.baud!=record.baud || check.enrollment!=record.enrollment)return {};
        return record;
    }
    static bool active_fresh(const Sample& sample) {
        return fresh(sample) && sample.bus_generation==cottage_pump_uart.generation();
    }
    static bool approved() {
        return state.limits.heating && state.approved_bus==cottage_pump_uart.generation() &&
            state.approved_revision==revision.load() && state.approved_link.current() &&
            state.limits.step_c==Protocol::step(state.active.climate);
    }
    static bool can_queue() {
        return approved() && active_fresh(state.active) && !state.busy && state.job.baud==0;
    }
    static bool changes_mode(const Settings& a,const Patch& b) {
        return (b.power && Protocol::power(a)!=b.power) || (b.heat && !Protocol::is_heat(a));
    }
    static bool authorized(const Job& job) {
        const auto& command=*job.command;
        if(!job.link.current() || !selected() ||
           command.revision!=revision.load() || command.bus!=cottage_pump_uart.generation() ||
           cottage_ota_network_maintenance())return false;
        return command.automatic?
            Protocol::automatic_write_current(command.authorization,command.config,
                                                          command.settings,command.proposal):
            !Protocol::automatic_enabled();
    }
    static void queue(Command command) {
        command.queued_utc=time(nullptr);
        state.job={state.pair.baud,state.active.link,false,command};
        state.requested={command.patch,command.queued_utc};
        state.sent={};
        state.status="write_queued";
    }
    static std::optional<double> parse_target_c(const String& argument) {
        const auto* first=argument.c_str();
        char* end=nullptr;
        const double c=std::strtod(first,&end);
        if(end==first || *end || c<Protocol::min_target || c>Protocol::max_target || !std::isfinite(c))return {};
        return c;
    }
    static void disconnect() {
        cottage_pump_uart.end(Protocol::owner);
        active_baud=0;
    }
    static Sample poll(uint32_t baud,const Link& link) {
        if(!Protocol::valid_baud(baud) || !link.current())return {};
        if(!cottage_pump_uart.owned_by(Protocol::owner) || baud!=active_baud ||
           active_link.endpoint!=link.endpoint || active_link.wifi!=link.wifi) {
            disconnect();
            cottage_pump_uart.begin(Protocol::owner,baud,Protocol::format);
            active_baud=baud;active_link=link;
            if(!Protocol::connect(link)) {disconnect();return {};}
        }
        Sample out;
        if(!Protocol::fill(out,link)) {disconnect();return {};}
        out.link=link;
        out.baud=baud;out.bus_generation=cottage_pump_uart.generation();
        out.received_ms=millis();
        out.received=link.current();
        return out;
    }
    static void observe(const Sample& sample) {
        if(sample.received) {
            const bool continuous=fresh(state.active) && state.active.bus_generation==sample.bus_generation;
            const auto& previous=state.active.climate;
            const bool same_mode=sample.climate.power==previous.power && sample.climate.mode==previous.mode;
            if(!continuous || !same_mode) {
                state.mode_since=sample.received_ms;
                state.dwell.command_dispatched(sample.received_ms);
            }
            if(const auto power=Protocol::power(sample.climate))
                state.dwell.observe(*power,sample.received_ms,true);
            if(!continuous || !Protocol::same_settings(sample.climate,previous)) {
                state.request_since=sample.received_ms;
                ++state.request_generation;
                state.automatic_queued_utc=state.automatic_confirmed_utc=0;
            }
        }
        state.active=sample;
    }
    static void activate(const PairRecord& pair,const Sample& sample) {
        state.pair=pair;state.active={};
        observe(sample);
        state.staged={};state.draft_id=0;
        state.limits={};state.approved_model[0]=0;
        state.requested={};state.sent={};state.confirmed={};
        revision.fetch_add(1);
        Protocol::automatic_cancel();
        state.status="ok";
    }
    struct WriteResult {
        const char* status="write_cancelled";
        Sample reading{};
        bool observed=false,confirmed=false;
    };
    static WriteResult perform_write(const Job& job,const PumpHeatingLimits& limits) {
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
           !Protocol::same_settings(before.climate,command.previous)) {
            result.status="external_change";return result;
        }
        if(Protocol::readback_matches(before.climate,before.climate,command.patch)) {
            result.status="already_applied";return result;
        }
        if(!allowed) {result.status="mode_dwell";return result;}
        const auto frame=Protocol::command(before.climate,command.patch,limits);
        if(!frame || !permit) {result.status="unsupported_command";return result;}
        if(!cottage_pump_uart.drain() || !authorized(job))return result;
        if(!cottage_pump_uart.send(frame->bytes.data(),frame->size)) {result.status="send_failed";return result;}
        xSemaphoreTake(gate,portMAX_DELAY);
        if(job.link.current() && command.bus==cottage_pump_uart.generation()) {
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
               Protocol::readback_matches(before.climate,after.climate,command.patch) && authorized(job)) {
                result.confirmed=true;result.status="confirmed";break;
            }
        }
        return result;
    }
    static void receipt(cJSON* root,const char* key,const Receipt& value) {
        if(value.utc<1700000000)return;
        auto* out=cJSON_AddObjectToObject(root,key);
        if(!out)return;
        cJSON_AddNumberToObject(out,"utc",value.utc);
        if(value.patch.power)cJSON_AddBoolToObject(out,"power",*value.patch.power);
        if(value.patch.target_c)cJSON_AddNumberToObject(out,"target_c",*value.patch.target_c);
    }
    static bool access(WebServer& web,bool mutation) {
        if(!cottage_wifi_require_home_lan(web))return false;
        if(!gate) {web.send(503,"application/json","{}");return false;}
        if(mutation && (!web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(web.arg("token").c_str()))) {
            web.send(403,"application/json","{\"error\":\"authorization\"}");return false;
        }
        return true;
    }
    static void status(WebServer& web) {
        const bool draft=web.arg("draft")=="true";
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool staged=draft && state.draft_id && state.staged.link.current();
        const auto sample=staged?state.staged:state.active;
        const auto baud=staged?sample.baud:state.pair.baud;
        const auto id=staged?state.draft_id:0;
        const auto result=draft?state.staged_status:state.status;
        const bool busy=state.busy || state.job.baud!=0;
        const bool pilot=!draft && selected() && approved();
        const bool writable=pilot && can_queue() && !Protocol::automatic_enabled();
        const auto limits=state.limits;
        const String model=state.approved_model;
        const auto requested=state.requested,sent=state.sent,confirmed=state.confirmed;
        const auto generation=revision.load();
        xSemaphoreGive(gate);

        auto* root=cJSON_CreateObject();
        if(!root) {web.send(503,"application/json","{}");return;}
        const bool current=fresh(sample) && (staged || sample.bus_generation==cottage_pump_uart.generation());
        cJSON_AddBoolToObject(root,"configured",Protocol::valid_baud(baud));
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
        if(Protocol::valid_baud(baud))cJSON_AddNumberToObject(root,"baud",baud);
        if(current) {
            if(const auto power=Protocol::power(sample.climate))cJSON_AddBoolToObject(root,"power",*power);
            if(const auto mode=Protocol::mode(sample.climate))cJSON_AddNumberToObject(root,"mode",*mode);
            if(sample.climate.target_c)cJSON_AddNumberToObject(root,"target_c",*sample.climate.target_c);
            if(const auto step=Protocol::step(sample.climate);step>0)
                cJSON_AddNumberToObject(root,"observed_step_c",step);
            if(sample.temperature.indoor_c)cJSON_AddNumberToObject(root,"indoor_c",*sample.temperature.indoor_c);
            if(sample.temperature.outdoor_c)cJSON_AddNumberToObject(root,"outdoor_c",*sample.temperature.outdoor_c);
            if(sample.compressor_hz)cJSON_AddNumberToObject(root,"compressor_hz",*sample.compressor_hz);
        }
        char* payload=cJSON_PrintUnformatted(root);
        web.send(payload?200:503,"application/json",payload?payload:"{}");
        cJSON_free(payload);cJSON_Delete(root);
    }
public:
    static bool selected() {
        return !std::strcmp(site_settings_snapshot().pump_adapter,Protocol::adapter_id);
    }
    static bool needs_worker() {
        if(!gate)return false;
        if(selected() || cottage_pump_uart.owned_by(Protocol::owner))return true;
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool pending=state.job.baud!=0;
        xSemaphoreGive(gate);
        return pending;
    }
    static void register_routes(WebServer& web) {
        gate=xSemaphoreCreateMutex();
        if(!gate)return;
        Preferences prefs;
        if(prefs.begin(Protocol::nvs,true)) {
            PairRecord pair{};
            if(prefs.getBytesLength("pair1")==sizeof(pair) &&
               prefs.getBytes("pair1",&pair,sizeof(pair))==sizeof(pair) && Protocol::valid_baud(pair.baud))
                state.pair=pair;
            else if constexpr(Protocol::legacy_baud) {
                const auto legacy=prefs.getUInt("baud");
                if(Protocol::valid_baud(legacy))state.pair.baud=legacy;
            }
        }
        web.on((String(Protocol::api_root)+"/status").c_str(),HTTP_GET,[&web](){
            if(access(web,false))status(web);
        });
        web.on((String(Protocol::api_root)+"/test").c_str(),HTTP_POST,[&web](){
            if(!access(web,true))return;
            const auto baud=Protocol::requested_baud(web);
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
        web.on((String(Protocol::api_root)+"/pilot").c_str(),HTTP_POST,[&web](){
            if(!access(web,true))return;
            const String enable=web.arg("enabled"),model=web.arg("model"),expected=web.arg("revision");
            const auto low=parse_target_c(web.arg("min")),high=parse_target_c(web.arg("max"));
            const bool requested=enable=="true";
            if((!requested && enable!="false") ||
               (requested && (!low || !high || *low>=*high || model.length()<3 || model.length()>47))) {
                web.send(400,"application/json","{\"error\":\"model_limits\"}");return;
            }
            xSemaphoreTake(gate,portMAX_DELAY);
            const auto step=Protocol::step(state.active.climate);
            const auto limits=requested?PumpHeatingLimits{*low,*high,step,true,true}:PumpHeatingLimits{};
            const bool allowed=selected() && !state.busy && state.job.baud==0 &&
                (!requested || (expected==String(revision.load()) && active_fresh(state.active) &&
                                limits.accepts(*low) && limits.accepts(*high)));
            if(allowed) {
                Protocol::automatic_cancel();
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
        web.on((String(Protocol::api_root)+"/control").c_str(),HTTP_POST,[&web](){
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
            const bool ready=selected() && !Protocol::automatic_enabled() &&
                expected==String(revision.load()) && can_queue();
            const bool supported=ready && Protocol::command(reading,patch,state.limits).has_value();
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
        web.on((String(Protocol::api_root)+"/commit").c_str(),HTTP_POST,[&web](){
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
    static uint32_t configuration_generation() {return revision.load();}
    static uint32_t bus_generation() {return cottage_pump_uart.generation();}
    static bool automatic_ready() {
        if(!gate || !selected())return false;
        xSemaphoreTake(gate,portMAX_DELAY);
        const auto mode=Protocol::heating_mode(state.active.climate);
        const bool enabled=can_queue() && (mode==PumpRoomMode::Heat || mode==PumpRoomMode::Off);
        xSemaphoreGive(gate);
        return enabled;
    }
    static ControlBlock submit_automatic(const PumpRoomState& earlier,ControlSnapshot proposal,
            uint32_t authorization,uint32_t config,uint32_t settings) {
        if(!gate || !Protocol::automatic_enabled())return ControlBlock::Disabled;
        xSemaphoreTake(gate,portMAX_DELAY);
        ControlBlock result=ControlBlock::InputsUnverified;
        if(can_queue() && earlier.configuration_generation==revision.load() &&
           earlier.request_generation==state.request_generation &&
           earlier.link_generation==state.active.bus_generation &&
           earlier.endpoint_generation==state.active.link.endpoint) {
            const auto& reading=state.active.climate;
            const auto mode=Protocol::heating_mode(reading);
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
    static uint32_t calibration_identity() {
        if(!gate)return 0;
        xSemaphoreTake(gate,portMAX_DELAY);
        const auto id=state.pair.enrollment?state.pair.enrollment:state.pair.baud;
        xSemaphoreGive(gate);
        return id;
    }
    static PumpRoomState room_state() {
        PumpRoomState out{};
        if(!gate || !selected())return out;
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
        if(!fresh(sample) || sample.bus_generation!=cottage_pump_uart.generation() ||
           sample.received_utc<1700000000)return out;
        out.source=PumpStateSource::VerifiedBus;
        out.protocol=Protocol::protocol;
        out.type=HeatPumpType::AirToAir;
        out.endpoint_generation=sample.link.endpoint;out.link_generation=sample.bus_generation;
        out.mode=Protocol::heating_mode(sample.climate);
        if(sample.climate.target_c)out.setpoint_c=*sample.climate.target_c;
        if(sample.temperature.indoor_c)out.indoor_c=*sample.temperature.indoor_c;
        if(sample.temperature.outdoor_c)out.outdoor_c=*sample.temperature.outdoor_c;
        out.evidence={sample.received_utc,sample.received_utc,true};
        return out;
    }
    static void tick() {
        static uint32_t last=0,last_endpoint=0,last_wifi=0;
        if(cottage_ota_network_maintenance())return;
        const auto link=live_link();
        const bool selected=selected();
        xSemaphoreTake(gate,portMAX_DELAY);
        auto job=state.job;
        const bool explicit_test=job.baud!=0;
        if(!explicit_test && selected && Protocol::valid_baud(state.pair.baud) &&
           (millis()-last>=poll_interval || link.endpoint!=last_endpoint || link.wifi!=last_wifi))
            job={state.pair.baud,link};
        const auto limits=state.limits;
        state.job={};
        state.busy=job.baud!=0;
        xSemaphoreGive(gate);
        if(!job.baud) {
            if(!selected && cottage_pump_uart.owned_by(Protocol::owner))disconnect();
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
                if(selected() && job.command->revision==revision.load()) {
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
};
}
