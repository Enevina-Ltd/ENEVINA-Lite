#ifndef COTTAGE_BENCH_MODE
#include "cottage_gree_service.hpp"
#include "cottage_air_service.hpp"
#include "cottage_network_budget.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "pump/gree_lan_protocol.hpp"
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cJSON.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
#include <cmath>
#include <ctime>

namespace {
using namespace enevina;
using namespace enevina::cottage;
using namespace enevina::gree;
constexpr char adapter_id[]="gree_ewpe_lan";
constexpr uint32_t config_version=1;
constexpr uint32_t poll_ms=30000;
constexpr uint32_t response_ms=1200;
constexpr uint32_t fresh_s=90;
constexpr size_t max_candidates=12;

struct Config {
    uint32_t version=0;
    char host[16]{},device[18]{};
    Cipher cipher=Cipher::Ecb;
    Key key{};
};
struct Link {
    uint32_t endpoint=0,wifi=0;
    bool current() const {
        return WiFi.status()==WL_CONNECTED &&
            wifi==cottage_wifi_link_generation() &&
            endpoint==site_settings_endpoint_generation();
    }
};
struct Sample {
    std::optional<Reading> reading;
    int64_t utc=0;
    uint32_t received_ms=0;
    Link link{};
    uint32_t config=0;
};
struct Candidate {
    char host[16]{},device[18]{};
    Cipher cipher=Cipher::Ecb;
};
enum class Work { None, Scan, Pair, Read, Write };
struct Request {
    Patch patch{};
    Reading previous{};
    uint32_t revision=0,authorization=0,settings=0,mode_config=0;
    BootstrapDecision proposal{};
    int64_t queued=0;
    bool automatic=false;
};
struct Job {
    Work work=Work::None;
    Config proposed{};
    Link link{};
    bool draft=false;
    Request request{};
};
struct Draft {
    Config config{};
    Sample sample{};
    uint32_t id=0;
};
struct Receipt {
    Patch patch{};
    int64_t utc=0;
};
struct State {
    Config saved{};
    uint32_t revision=0;
    Sample sample{};
    Draft draft{};
    uint32_t next_draft=0;
    std::array<Candidate,max_candidates> candidates{};
    size_t candidate_count=0;
    Link scan_link{};
    Job queued{};
    bool busy=false;
    const char* result="not_tested";
    const char* draft_result="not_tested";
    uint32_t pilot_revision=0,pilot_endpoint=0;
    char pilot_model[48]{};
    PumpHeatingLimits limits{};
    MitsubishiModeHistory dwell{};
    uint32_t mode_since_ms=0,request_since_ms=0,request_generation=0;
    int64_t automatic_confirmed_utc=0,automatic_queued_utc=0;
    Receipt requested{},sent{},confirmed{};
};

SemaphoreHandle_t guard=nullptr;
std::atomic<uint32_t> active_revision{0};
State state{};
Link active_link() {
    return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};
}
bool configured(const Config& c) {return c.version==config_version;}
bool fresh(const Sample& s,uint32_t revision) {
    return s.reading && s.link.current() && s.config==revision &&
        static_cast<uint32_t>(millis()-s.received_ms)<=fresh_s*1000;
}

bool heating(const Reading& r) {return r.power && r.mode==Mode::Heat;}
bool same_request(const Reading& a,const Reading& b) {
    return a.power==b.power && a.mode==b.mode && a.setpoint_c==b.setpoint_c && a.frost==b.frost;
}
void publish_reading(const std::optional<Reading>& reading,const Link& link) {
    const uint32_t now=millis();
    const bool continuous=fresh(state.sample,state.revision);
    if(!reading || !continuous || !same_request(*reading,*state.sample.reading)) {
        ++state.request_generation;
        state.request_since_ms=now;
    }
    if(!reading || !continuous || reading->power!=state.sample.reading->power ||
       (reading->power && reading->mode!=state.sample.reading->mode))
        state.mode_since_ms=now;
    if(reading) {
        if(!continuous)state.dwell.reset(now);
        state.dwell.observe(heating(*reading),now,true);
    }
    state.sample={reading,time(nullptr),now,link,state.revision};
}
void install(const Config& config,const std::optional<Reading>& reading,const Link& link) {
    state.saved=config;
    ++state.revision;
    // Publish the new identity before the worker or settings can reuse the old pilot.
    active_revision.store(state.revision);
    state.limits={};
    state.pilot_model[0]=0;
    state.requested={};state.sent={};state.confirmed={};
    state.automatic_confirmed_utc=0;state.automatic_queued_utc=0;
    publish_reading(reading,link);
}
uint32_t dwell_elapsed() {
    const auto now=millis();
    return std::min(static_cast<uint32_t>(now-state.mode_since_ms),state.dwell.since_change(now));
}
bool can_queue() {
    return !state.busy && state.queued.work==Work::None &&
        state.limits.heating && state.pilot_revision==state.revision &&
        state.pilot_endpoint==site_settings_endpoint_generation() &&
        fresh(state.sample,state.revision) && heating_state_known(*state.sample.reading);
}
void queue_command(Request request,const Link& link) {
    request.queued=time(nullptr);
    state.queued={Work::Write,{},link,false,request};
    state.requested={request.patch,request.queued};
    state.sent={};
    state.result="write_queued";
}
bool local_peer(const IPAddress& ip) {
    const uint8_t a=ip[0],b=ip[1];
    const bool private_ip=a==10 || (a==192 && b==168) || (a==172 && b>=16 && b<=31);
    if(!private_ip)return false;
    const auto local=static_cast<uint32_t>(WiFi.localIP());
    const auto mask=static_cast<uint32_t>(WiFi.subnetMask());
    const auto target=static_cast<uint32_t>(ip);
    const auto network=local&mask;
    return local && mask && (target&mask)==network && target!=local &&
        target!=network && target!=(network|~mask);
}
bool target_ip(const char* text,IPAddress& ip) {
    return text && ip.fromString(text) && local_peer(ip);
}
bool valid_config(const Config& c) {
    IPAddress address;
    return configured(c) && std::memchr(c.host,0,sizeof(c.host)) &&
        std::memchr(c.device,0,sizeof(c.device)) && address.fromString(c.host) &&
        device_id(c.device) && (c.cipher==Cipher::Ecb || c.cipher==Cipher::Gcm) &&
        std::all_of(c.key.begin(),c.key.end(),[](uint8_t c){return c>=32 && c<=126;});
}
void load_config() {
    Preferences nvs;
    Config config{};
    if(nvs.begin("cottage_gree",true) && nvs.getBytesLength("paired1")==sizeof(config) &&
       nvs.getBytes("paired1",&config,sizeof(config))==sizeof(config) && valid_config(config))
        state.saved=config;
}
bool store_config(const Config& c) {
    Preferences nvs;
    if(!nvs.begin("cottage_gree",false) || nvs.putBytes("paired1",&c,sizeof(c))!=sizeof(c))
        return false;
    Config copy{};
    return nvs.getBytes("paired1",&copy,sizeof(copy))==sizeof(copy) &&
        copy.version==c.version && !std::strcmp(copy.host,c.host) &&
        !std::strcmp(copy.device,c.device) && copy.cipher==c.cipher && copy.key==c.key;
}

bool send(WiFiUDP& udp,const IPAddress& ip,std::string_view packet) {
    return !packet.empty() && udp.begin(0) && udp.beginPacket(ip,port) &&
        udp.write(reinterpret_cast<const uint8_t*>(packet.data()),packet.size())==packet.size() &&
        udp.endPacket();
}
template<typename Decoder>
auto exchange(const IPAddress& ip,std::string_view packet,const Link& link,Decoder decode)
    -> decltype(decode(std::string_view{})) {
    WiFiUDP udp;
    if(!link.current() || !local_peer(ip) || !send(udp,ip,packet))return {};
    std::array<char,max_packet> bytes{};
    const uint32_t start=millis();
    while(link.current() && millis()-start<response_ms) {
        const int size=udp.parsePacket();
        if(size>0) {
            if(udp.remoteIP()!=ip || udp.remotePort()!=port || size>bytes.size()) {
                udp.flush();
                continue;
            }
            const auto count=udp.read(reinterpret_cast<uint8_t*>(bytes.data()),size);
            if(count==size) {
                auto value=decode(std::string_view(bytes.data(),count));
                if(value)return value;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    return {};
}

std::optional<Reading> read(const Config& c,const Link& link) {
    IPAddress ip;
    if(!target_ip(c.host,ip))return {};
    return exchange(ip,status_request(c.cipher,c.key,c.device),link,[&](std::string_view reply){
        return parse_status(c.cipher,c.key,c.device,reply);
    });
}
std::optional<Key> bind(Config& c,const Link& link) {
    IPAddress ip;
    if(!target_ip(c.host,ip))return {};
    // Some modules accept bind only immediately after discovery. Repeat a
    // unicast scan before binding and verify the response is from this pump.
    const auto found=exchange(ip,scan_packet,link,[&](std::string_view reply){
        auto d=discovery(reply);
        if(!d || d->cipher!=c.cipher || d->id!=device_id(c.device))return std::optional<Discovery>{};
        return d;
    });
    if(!found)return {};
    const auto key=exchange(ip,bind_request(c.cipher,found->wire_id),link,[&](std::string_view reply){
        return bound_key(c.cipher,found->wire_id,reply);
    });
    if(key)strlcpy(c.device,found->wire_id.c_str(),sizeof(c.device));
    return key;
}

size_t scan(const Link& link,std::array<Candidate,max_candidates>& out) {
    WiFiUDP udp;
    const auto local=static_cast<uint32_t>(WiFi.localIP());
    const auto mask=static_cast<uint32_t>(WiFi.subnetMask());
    if(!local || !mask || !link.current() || !send(udp,IPAddress(local|~mask),scan_packet))
        return 0;
    size_t count=0;
    std::array<char,max_packet> bytes{};
    const uint32_t start=millis();
    while(link.current() && millis()-start<response_ms && count<out.size()) {
        const int size=udp.parsePacket();
        if(size>0) {
            const auto ip=udp.remoteIP();
            if(!local_peer(ip) || udp.remotePort()!=port || size>bytes.size()) {
                udp.flush();
                continue;
            }
            const int n=udp.read(reinterpret_cast<uint8_t*>(bytes.data()),size);
            if(n==size) {
                if(const auto d=discovery(std::string_view(bytes.data(),n))) {
                    const String host=ip.toString();
                    bool duplicate=false;
                    for(size_t i=0;i<count;++i)
                        duplicate|=!std::strcmp(out[i].host,host.c_str()) &&
                            device_id(out[i].device)==d->id;
                    if(!duplicate) {
                        auto& item=out[count++];
                        strlcpy(item.host,host.c_str(),sizeof(item.host));
                        strlcpy(item.device,d->wire_id.c_str(),sizeof(item.device));
                        item.cipher=d->cipher;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    return count;
}

void receipt(cJSON* root,const char* name,const Receipt& value) {
    const auto& p=value.patch;
    if(!p.power && !p.mode && !p.setpoint_c)return;
    if(auto* entry=cJSON_AddObjectToObject(root,name)) {
        if(p.power)cJSON_AddBoolToObject(entry,"power",*p.power);
        if(p.mode)cJSON_AddNumberToObject(entry,"mode",static_cast<unsigned>(*p.mode));
        if(p.setpoint_c)cJSON_AddNumberToObject(entry,"target_c",*p.setpoint_c);
        if(value.utc>=1700000000)cJSON_AddNumberToObject(entry,"utc",value.utc);
    }
}
void status(WebServer& web,bool draft_view) {
    xSemaphoreTake(guard,portMAX_DELAY);
    const State s=state;
    xSemaphoreGive(guard);
    const bool staged=draft_view && s.draft.id && s.draft.sample.link.current();
    const auto& shown_config=staged?s.draft.config:s.saved;
    const auto& sample=staged?s.draft.sample:s.sample;
    const bool is_fresh=fresh(sample,staged?s.draft.id:s.revision);
    auto* root=cJSON_CreateObject();
    if(!root) {web.send(503,"application/json","{}");return;}
    cJSON_AddBoolToObject(root,"configured",configured(shown_config));
    cJSON_AddBoolToObject(root,"fresh",is_fresh);
    if(staged)cJSON_AddNumberToObject(root,"draft_id",s.draft.id);
    cJSON_AddNumberToObject(root,"revision",s.revision);
    const bool selected=cottage_gree_selected();
    const bool approved=selected && s.limits.heating && s.pilot_revision==s.revision &&
        s.pilot_endpoint==site_settings_endpoint_generation();
    const bool automatic=selected && cottage_gree_automatic_enabled();
    cJSON_AddBoolToObject(root,"pilot_approved",approved);
    cJSON_AddBoolToObject(root,"automatic",automatic);
    cJSON_AddBoolToObject(root,"write_available",approved && !automatic && !staged &&
        is_fresh && s.sample.reading && heating_state_known(*s.sample.reading));
    if(approved) {
        cJSON_AddStringToObject(root,"pilot_model",s.pilot_model);
        cJSON_AddNumberToObject(root,"minimum_c",s.limits.minimum_c);
        cJSON_AddNumberToObject(root,"maximum_c",s.limits.maximum_c);
    }
    if(selected) {
        receipt(root,"requested",s.requested);
        receipt(root,"sent",s.sent);
        receipt(root,"confirmed",s.confirmed);
    }
    cJSON_AddBoolToObject(root,"busy",s.busy || s.queued.work!=Work::None);
    cJSON_AddStringToObject(root,"result",draft_view?s.draft_result:s.result);
    if(configured(shown_config)) {
        cJSON_AddStringToObject(root,"host",shown_config.host);
        cJSON_AddStringToObject(root,"device",shown_config.device);
        cJSON_AddNumberToObject(root,"cipher",static_cast<unsigned>(shown_config.cipher));
    }
    if(is_fresh) {
        const auto& r=*sample.reading;
        cJSON_AddBoolToObject(root,"power",r.power);
        cJSON_AddNumberToObject(root,"mode",static_cast<unsigned>(r.mode));
        if(r.setpoint_c)cJSON_AddNumberToObject(root,"target_c",*r.setpoint_c);
        if(r.indoor_c)cJSON_AddNumberToObject(root,"indoor_c",*r.indoor_c);
        if(r.frost)cJSON_AddBoolToObject(root,"frost",*r.frost);
        if(sample.utc>=1700000000)cJSON_AddNumberToObject(root,"read_utc",sample.utc);
    }
    if(auto* found=cJSON_AddArrayToObject(root,"discovered");found && s.scan_link.current()) {
        for(size_t i=0;i<s.candidate_count;++i) {
            auto* item=cJSON_CreateObject();
            if(!item)break;
            cJSON_AddItemToArray(found,item);
            cJSON_AddStringToObject(item,"host",s.candidates[i].host);
            cJSON_AddStringToObject(item,"device",s.candidates[i].device);
            cJSON_AddNumberToObject(item,"cipher",static_cast<unsigned>(s.candidates[i].cipher));
        }
    }
    char* data=cJSON_PrintUnformatted(root);
    web.send(data?200:503,"application/json",data?data:"{}");
    cJSON_free(data);
    cJSON_Delete(root);
}

bool web_ready(WebServer& web,bool write) {
    if(!cottage_wifi_require_home_lan(web))return false;
    if(!guard) {
        web.send(503,"application/json","{\"error\":\"unavailable\"}");return false;
    }
    if(write && (!web.hasArg("token") ||
       !enevina::cottage::c23_authorize_local_setting(web.arg("token").c_str()))) {
        web.send(403,"application/json","{\"error\":\"authorization\"}");return false;
    }
    return true;
}
void enqueue(WebServer& web,Work work,Config proposed={}) {
    xSemaphoreTake(guard,portMAX_DELAY);
    const bool pending=state.busy || state.queued.work!=Work::None;
    if(!pending)state.queued={work,proposed,active_link(),web.arg("draft")=="true"};
    xSemaphoreGive(guard);
    web.send(pending?409:202,"application/json",pending?
        "{\"error\":\"busy\"}":"{\"queued\":true}");
}
bool value_between(const String& arg,int& out) {
    if(arg.length()!=2 || arg[0]<'0' || arg[0]>'9' || arg[1]<'0' || arg[1]>'9')return false;
    out=(arg[0]-'0')*10+(arg[1]-'0');
    return out>=16 && out<=30;
}
bool command_current(const Job& job) {
    if(!job.link.current() || !cottage_gree_selected() || cottage_ota_network_maintenance() ||
       job.request.revision!=active_revision.load())return false;
    if(!job.request.automatic)return !cottage_gree_automatic_enabled();
    return cottage_gree_automatic_write_current(job.request.authorization,job.request.mode_config,
                                                job.request.settings,job.request.proposal);
}
struct WriteOutcome {
    const char* result="write_cancelled";
    std::optional<Reading> reading;
    int64_t sent_utc=0,confirmed_utc=0;
    bool sent=false,confirmed=false;
};
WriteOutcome write_command(const Job& job,const Config& config,const PumpHeatingLimits& limits) {
    WriteOutcome outcome;
    const auto& request=job.request;
    if(!command_current(job))return outcome;
    outcome.reading=read(config,job.link);
    if(!outcome.reading) {outcome.result="read_failed";return outcome;}
    // Let a fresh preflight inform the same room source and next plan, including
    // external remote changes. Do not send an automatic proposal based on an old state.
    xSemaphoreTake(guard,portMAX_DELAY);
    if(job.link.current() && request.revision==state.revision)
        publish_reading(outcome.reading,job.link);
    const bool mode_allowed=dwell_allows(request.patch,*outcome.reading,dwell_elapsed(),
                                        mitsubishi_min_mode_dwell_ms);
    xSemaphoreGive(guard);
    const auto before=*outcome.reading;
    if(request.automatic && !same_request(before,request.previous)) {
        outcome.result="external_change";return outcome;
    }
    if(!permits_heating_patch(request.patch,before,limits)) {outcome.result="unsupported";return outcome;}
    if(confirmed(request.patch,before)) {outcome.result="already_applied";return outcome;}
    if(!mode_allowed) {outcome.result="mode_dwell";return outcome;}
    IPAddress target;
    const auto packet=command_request(config.cipher,config.key,config.device,request.patch);
    if(!target_ip(config.host,target) || packet.empty() || !command_current(job))return outcome;
    {
        WiFiUDP udp;
        outcome.sent=send(udp,target,packet);
    }
    if(!outcome.sent) {outcome.result="send_failed";return outcome;}
    outcome.sent_utc=time(nullptr);
    xSemaphoreTake(guard,portMAX_DELAY);
    if(job.link.current() && request.revision==state.revision) {
        state.sent={request.patch,outcome.sent_utc};
        if(changes_operating_mode(request.patch,before))
            state.dwell.command_dispatched(millis());
    }
    xSemaphoreGive(guard);
    // A new status datagram on its own socket verifies only this exact write.
    // A SET acknowledgement or a preflight value cannot reach this branch.
    outcome.reading.reset();
    for(unsigned i=0;i<3 && job.link.current();++i) {
        vTaskDelay(pdMS_TO_TICKS(350));
        if(const auto after=read(config,job.link)) {
            outcome.reading=after;
            if(confirmed(request.patch,*after)) {
                outcome.confirmed=true;
                outcome.confirmed_utc=time(nullptr);
                break;
            }
        }
    }
    outcome.result=outcome.confirmed?"confirmed":"unconfirmed";
    return outcome;
}
}

bool cottage_gree_selected() {
    return std::strcmp(site_settings_snapshot().pump_adapter,adapter_id)==0;
}
bool cottage_gree_needs_worker() {
    if(!guard)return false;
    if(cottage_gree_selected())return true;
    xSemaphoreTake(guard,portMAX_DELAY);
    const bool requested=state.queued.work!=Work::None;
    xSemaphoreGive(guard);
    return requested;
}
void cottage_gree_register_routes(WebServer& web) {
    guard=xSemaphoreCreateMutex();
    if(!guard)return;
    load_config();
    web.on("/api/gree/status",HTTP_GET,[&web](){
        if(web_ready(web,false))status(web,web.arg("draft")=="true");
    });
    web.on("/api/gree/discover",HTTP_POST,[&web](){
        if(web_ready(web,true))enqueue(web,Work::Scan);
    });
    web.on("/api/gree/test",HTTP_POST,[&web](){
        if(web_ready(web,true))enqueue(web,Work::Read);
    });
    web.on("/api/gree/pair",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const String host=web.arg("host"),device=web.arg("device"),cipher=web.arg("cipher");
        IPAddress ip;
        if(!target_ip(host.c_str(),ip) || !device_id(device.c_str()) ||
           (cipher!="1" && cipher!="2")) {
            web.send(400,"application/json","{\"error\":\"invalid_device\"}");return;
        }
        Config proposed{};
        proposed.cipher=cipher=="2"?Cipher::Gcm:Cipher::Ecb;
        strlcpy(proposed.host,ip.toString().c_str(),sizeof(proposed.host));
        strlcpy(proposed.device,device.c_str(),sizeof(proposed.device));
        enqueue(web,Work::Pair,proposed);
    });
    web.on("/api/gree/pilot",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const String model=web.arg("model"),id=web.arg("device"),enable=web.arg("enabled");
        if(!cottage_gree_selected() || (enable!="true" && enable!="false")) {
            web.send(409,"application/json","{\"error\":\"unsupported\"}");return;
        }
        const bool requested=enable=="true";
        int minimum=0,maximum=0;
        if(requested && (model.length()<3 || model.length()>47 || !device_id(id.c_str()) ||
           !value_between(web.arg("min"),minimum) || !value_between(web.arg("max"),maximum) ||
           minimum>=maximum)) {
            web.send(400,"application/json","{\"error\":\"model_limits\"}");return;
        }
        xSemaphoreTake(guard,portMAX_DELAY);
        const bool same=device_id(id.c_str())==device_id(state.saved.device);
        const bool allowed=!state.busy && state.queued.work==Work::None &&
            (!requested || (same && fresh(state.sample,state.revision) &&
                            heating_state_known(*state.sample.reading)));
        if(allowed) {
            cottage_gree_automatic_cancel();
            state.pilot_revision=state.revision;
            state.pilot_endpoint=site_settings_endpoint_generation();
            strlcpy(state.pilot_model,requested?model.c_str():"",sizeof(state.pilot_model));
            state.limits=requested?PumpHeatingLimits{static_cast<double>(minimum),
                static_cast<double>(maximum),1,true,true}:PumpHeatingLimits{};
            state.dwell.reset(millis());
        }
        xSemaphoreGive(guard);
        web.send(allowed?200:409,"application/json",allowed?"{\"saved\":true}":"{\"error\":\"pilot_required\"}");
    });
    web.on("/api/gree/control",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const String action=web.arg("action"),revision=web.arg("revision");
        const String target=web.arg("target");
        Patch patch;
        int setpoint=0;
        if(action=="off")patch.power=false;
        else if((action=="target" || action=="heat") && value_between(target,setpoint)) {
            patch.setpoint_c=setpoint;
            if(action=="heat") {patch.mode=Mode::Heat;patch.power=true;}
        } else {
            web.send(400,"application/json","{\"error\":\"unsupported\"}");return;
        }
        xSemaphoreTake(guard,portMAX_DELAY);
        const bool ready=cottage_gree_selected() && !cottage_gree_automatic_enabled() &&
            revision==String(state.revision) && can_queue() &&
            permits_heating_patch(patch,*state.sample.reading,state.limits);
        const bool allowed=ready && dwell_allows(patch,*state.sample.reading,dwell_elapsed(),
                                               mitsubishi_min_mode_dwell_ms);
        if(allowed) {
            Request request{};
            request.patch=patch;
            request.revision=state.revision;
            queue_command(request,active_link());
        }
        xSemaphoreGive(guard);
        web.send(allowed?202:409,"application/json",allowed?"{\"queued\":true}":
            ready?"{\"error\":\"mode_dwell\"}":"{\"error\":\"pilot_required\"}");
    });
    web.on("/api/gree/commit",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const String expected=web.arg("draft_id");
        xSemaphoreTake(guard,portMAX_DELAY);
        const bool matched=state.draft.id && expected==String(state.draft.id) &&
            fresh(state.draft.sample,state.draft.id) &&
            !state.busy && state.queued.work==Work::None;
        const bool saved=matched && store_config(state.draft.config);
        if(saved) {
            install(state.draft.config,state.draft.sample.reading,state.draft.sample.link);
            state.sample.utc=state.draft.sample.utc;
            state.sample.received_ms=state.draft.sample.received_ms;
            state.result="paired";
            state.draft={};
        }
        xSemaphoreGive(guard);
        web.send(saved?200:matched?500:409,"application/json",saved?
            "{\"saved\":true}":matched?"{\"error\":\"store_failed\"}":"{\"error\":\"read_failed\"}");
    });
}

uint32_t cottage_gree_configuration_generation() {return active_revision.load();}
bool cottage_gree_automatic_ready() {
    if(!guard || !cottage_gree_selected())return false;
    xSemaphoreTake(guard,portMAX_DELAY);
    const bool ready=can_queue();
    xSemaphoreGive(guard);
    return ready;
}
uint32_t cottage_gree_calibration_identity() {
    if(!guard)return 0;
    xSemaphoreTake(guard,portMAX_DELAY);
    const auto config=state.saved;
    xSemaphoreGive(guard);
    if(!configured(config))return 0;
    const auto id=device_id(config.device);
    if(!id)return 0;
    uint32_t hash=2166136261u;
    for(const char digit:*id) {hash^=static_cast<uint8_t>(digit);hash*=16777619u;}
    hash^=static_cast<uint8_t>(config.cipher);
    return hash*16777619u;
}
ControlBlock cottage_gree_submit_automatic(const PumpRoomState& earlier,ControlSnapshot proposal,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    if(!guard || !cottage_gree_automatic_enabled())return ControlBlock::Disabled;
    xSemaphoreTake(guard,portMAX_DELAY);
    ControlBlock result=ControlBlock::InputsUnverified;
    if(can_queue() && earlier.configuration_generation==state.revision &&
       earlier.request_generation==state.request_generation &&
       earlier.endpoint_generation==state.sample.link.endpoint &&
       earlier.link_generation==state.sample.link.wifi) {
        const auto& reading=*state.sample.reading;
        if(!reading.power || reading.mode==Mode::Heat) {
            const auto initial=proposal.proposal;
            if(initial.action==BootstrapAction::HeatSetpoint)
                proposal.proposal.heat_setpoint_c=std::clamp(std::round(initial.heat_setpoint_c),
                    state.limits.minimum_c,state.limits.maximum_c);
            proposal.current_heat=heating(reading);
            proposal.current_setpoint_c=*reading.setpoint_c;
            proposal.cn105_link_fresh=proposal.bridge_get_ok=true;
            proposal.ms_since_mode_change=dwell_elapsed();
            const auto decision=choose_pump_control(proposal,state.limits,HeatTransition::Combined);
            result=decision.gate;
            if(decision.step!=ControlStep::None) {
                Request request{};
                request.previous=reading;
                request.revision=state.revision;
                request.authorization=authorization;
                request.mode_config=config;
                request.settings=settings;
                request.proposal=initial;
                request.automatic=true;
                if(decision.step==ControlStep::SetOff)request.patch.power=false;
                else {
                    request.patch.setpoint_c=static_cast<int>(decision.target_c);
                    if(decision.step==ControlStep::SetHeat) {
                        request.patch.mode=Mode::Heat;request.patch.power=true;
                    }
                }
                queue_command(request,state.sample.link);
            }
        }
    }
    xSemaphoreGive(guard);
    return result;
}
enevina::PumpRoomState cottage_gree_room_state() {
    using namespace enevina;
    PumpRoomState out{};
    if(!guard || !cottage_gree_selected())return out;
    xSemaphoreTake(guard,portMAX_DELAY);
    const auto s=state.sample;
    const auto revision=state.revision;
    const auto cipher=state.saved.cipher;
    out.heating=state.limits;
    out.request_generation=state.request_generation;
    const auto now=millis();
    out.mode_minutes=static_cast<uint32_t>(now-state.mode_since_ms)/60000;
    out.request_minutes=static_cast<uint32_t>(now-state.request_since_ms)/60000;
    out.automatic_confirmed_utc=state.automatic_confirmed_utc;
    out.automatic_queued_utc=state.automatic_queued_utc;
    xSemaphoreGive(guard);
    if(!fresh(s,revision))return out;
    out.source=PumpStateSource::DirectLan;
    out.protocol=cipher==Cipher::Gcm?PumpRoomProtocol::GreeGcm:PumpRoomProtocol::GreeEcb;
    out.type=HeatPumpType::AirToAir;
    out.endpoint_generation=s.link.endpoint;
    out.link_generation=s.link.wifi;
    out.configuration_generation=revision;
    if(s.utc>=1700000000)out.evidence={s.utc,0,false};
    const auto& r=*s.reading;
    out.mode=heating_room_mode(r);
    if(r.setpoint_c)out.setpoint_c=*r.setpoint_c;
    if(r.indoor_c)out.indoor_c=*r.indoor_c;
    return out;
}

void cottage_gree_tick() {
    static uint32_t last_poll=0,last_wifi=0,last_endpoint=0;
    if(WiFi.status()!=WL_CONNECTED || cottage_ota_network_maintenance()) {
        return;
    }
    const auto link=active_link();
    const bool selected=cottage_gree_selected();
    xSemaphoreTake(guard,portMAX_DELAY);
    Job job=state.queued;
    if(job.work==Work::None && selected && configured(state.saved) &&
       (millis()-last_poll>=poll_ms || last_wifi!=link.wifi || last_endpoint!=link.endpoint))
        job={Work::Read,{},link};
    const bool using_draft=job.draft && state.draft.id && state.draft.sample.link.current();
    const Config old=using_draft?state.draft.config:state.saved;
    const auto limits=state.limits;
    state.queued={};
    state.busy=job.work!=Work::None;
    xSemaphoreGive(guard);
    if(job.work==Work::None) {
        return;
    }
    const char* result="link_changed";
    Config candidate=job.proposed;
    std::optional<Reading> reading;
    WriteOutcome command{};
    std::array<Candidate,max_candidates> found{};
    size_t count=0;
    if(job.link.current()) {
        enevina_firmware::NetworkBudgetLease lease(700,72*1024,16*1024);
        if(!lease.held())result="network_busy";
        else if(job.work==Work::Scan) {
            count=scan(job.link,found);
            result=count?"scan_ok":"no_devices";
        } else if(job.work==Work::Pair) {
            const bool same=configured(old) && !std::strcmp(candidate.host,old.host) &&
                candidate.cipher==old.cipher && device_id(candidate.device)==device_id(old.device);
            if(same)reading=read(old,job.link);
            if(reading) {
                candidate=old;
                result="paired";
            } else if(const auto key=bind(candidate,job.link)) {
                candidate.version=config_version;
                candidate.key=*key;
                reading=read(candidate,job.link);
                result=reading?"paired":"read_failed";
            } else result="bind_failed";
        } else if(job.work==Work::Write) {
            command=write_command(job,old,limits);
            result=command.result;
        } else {
            if(configured(old))reading=read(old,job.link);
            result=!configured(old)?"not_configured":reading?"ok":"read_failed";
        }
    }
    xSemaphoreTake(guard,portMAX_DELAY);
    state.busy=false;
    if(job.link.current()) {
        if(job.work==Work::Scan) {
            state.candidates=found;
            state.candidate_count=count;
            state.scan_link=job.link;
        } else if(job.work==Work::Pair && reading) {
            if(job.draft) {
                if(++state.next_draft==0)++state.next_draft;
                const uint32_t id=state.next_draft;
                state.draft={candidate,{reading,time(nullptr),millis(),job.link,id},id};
            } else if(store_config(candidate)) {
                install(candidate,reading,job.link);
                state.draft={};
            } else result="store_failed";
        } else if(job.work==Work::Write && job.request.revision==state.revision) {
            if(command.reading || command.sent)publish_reading(command.reading,job.link);
            if(command.confirmed) {
                state.confirmed={job.request.patch,command.confirmed_utc};
                if(job.request.automatic) {
                    state.automatic_queued_utc=job.request.queued;
                    state.automatic_confirmed_utc=command.confirmed_utc;
                }
            }
        } else if(job.work==Work::Read && using_draft) {
            state.draft.sample={reading,time(nullptr),millis(),job.link,state.draft.id};
        } else if(job.work==Work::Read) {
            publish_reading(reading,job.link);
        }
    } else result="link_changed";
    (job.draft?state.draft_result:state.result)=result;
    xSemaphoreGive(guard);
    if(job.work!=Work::Scan) {
        last_poll=millis();last_wifi=job.link.wifi;last_endpoint=job.link.endpoint;
    }
}
#endif
