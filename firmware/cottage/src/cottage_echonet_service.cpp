#ifndef COTTAGE_BENCH_MODE
#include "cottage_echonet_service.hpp"
#include "cottage/automatic_command_reconciliation.hpp"
#include "cottage_air_service.hpp"
#include "cottage_local_http.hpp"
#include "cottage_network_budget.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "pump/echonet_lan.hpp"

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cJSON.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <memory>
#include <new>

namespace {
using namespace enevina;
using namespace enevina::cottage;
namespace el=enevina::echonet;
constexpr char adapter_id[]="echonet_lite_lan";
constexpr uint32_t schema=1, poll_interval=30000, freshness=90000, scan_freshness=120000;
constexpr uint32_t poll_budget=5000, scan_budget=2200;

struct Link {
    uint32_t endpoint=0,wifi=0;
    bool current()const {
        return WiFi.status()==WL_CONNECTED && endpoint==site_settings_endpoint_generation() &&
            wifi==cottage_wifi_link_generation() && !cottage_ota_network_maintenance();
    }
    bool operator==(const Link& other)const{return endpoint==other.endpoint && wifi==other.wifi;}
};
Link live_link(){return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};}
struct Config {
    uint32_t version=0;
    el::Peer peer{};
    el::Identity identity{};
    bool valid()const{return version==schema && peer.valid() && identity.valid();}
    bool operator==(const Config& other)const {
        return valid() && other.valid() && peer==other.peer && identity==other.identity;
    }
};
struct Snapshot {
    Config config{};
    el::Capabilities caps{};
    el::State reading{};
    Link link{};
    uint32_t received_ms=0;
    int64_t received_utc=0;
    bool received=false;
};
bool fresh(const Snapshot& s){return s.received && s.link.current() && millis()-s.received_ms<=freshness;}
enum class Task:uint8_t {None,Scan,Identify,Read,Write};
struct Command {
    el::Desired desired{};
    el::State previous{};
    BootstrapDecision proposal{};
    uint32_t revision=0,authorization=0,config=0,settings=0;
    int64_t queued_utc=0;
    bool automatic=false;
};
struct Work {
    Task task=Task::None;
    Link link{};
    el::Peer peer{};
    bool draft=false;
    Command command{};
};
struct Receipt {el::Desired desired{};int64_t utc=0;bool present=false;};
struct Approval {
    el::ApprovedModes modes{};
    Config config{};
    el::Capabilities caps{};
    Link link{};
    uint32_t revision=0;
    char model[48]{};
};
struct State {
    Config saved{};
    Snapshot active{},staged{};
    Approval approval{};
    el::CandidateList candidates{};
    Link scan_link{};
    uint32_t scan_ms=0;
    bool scan_received=false,draft_started=false,busy=false;
    bool write_active=false;
    uint32_t write_generation=0;
    uint32_t stage_id=0,next_id=0,revision=0,request_generation=0,request_since_ms=0,mode_since_ms=0;
    int64_t automatic_queued_utc=0,automatic_confirmed_utc=0;
    MitsubishiModeHistory dwell{};
    AutomaticCommandReconciliation reconciliation{};
    Work pending{};
    Receipt requested{},sent{},confirmed{};
    const char* status="not_tested";
    const char* draft_status="not_tested";
};
State state{};
SemaphoreHandle_t gate=nullptr;
std::atomic<uint32_t> revision{0};

bool local_peer(uint32_t ipv4) {
    const IPAddress ip(ipv4);
    const auto a=ip[0],b=ip[1];
    if(!(a==10 || (a==192 && b==168) || (a==172 && b>=16 && b<=31)))return false;
    const uint32_t local=static_cast<uint32_t>(WiFi.localIP());
    const uint32_t mask=static_cast<uint32_t>(WiFi.subnetMask());
    const uint32_t network=local&mask;
    return local && mask && (ipv4&mask)==network && ipv4!=local &&
        ipv4!=network && ipv4!=(network|~mask);
}
String peer_name(const el::Peer& p){return IPAddress(p.ipv4).toString()+"/"+String(p.instance);}
bool persist(const Config& config) {
    Preferences p;
    if(!config.valid() || !p.begin("cottage_echo",false) ||
       p.putBytes("peer1",&config,sizeof(config))!=sizeof(config))return false;
    Config stored{};
    return p.getBytes("peer1",&stored,sizeof(stored))==sizeof(stored) && stored==config;
}
bool approved() {
    const auto& a=state.approval;
    return a.modes.valid() && a.link.current() && a.revision==revision.load() &&
        state.active.received && a.config==state.active.config && el::same_caps(a.caps,state.active.caps);
}
bool available() {
    return approved() && fresh(state.active) && state.active.reading.fault!=true &&
        !state.busy && state.pending.task==Task::None;
}
void clear_receipts() {
    state.requested={};state.sent={};state.confirmed={};
    state.automatic_queued_utc=state.automatic_confirmed_utc=0;
}
void bump_revision(){if(++state.revision==0)++state.revision;revision.store(state.revision);}
void revoke() {
    state.approval={};clear_receipts();bump_revision();cottage_echonet_automatic_cancel();
}
void observe(const Snapshot& sample) {
    if(!sample.received){state.active={};return;}
    if(state.approval.modes.valid() && (!(state.approval.config==sample.config) ||
       !el::same_caps(state.approval.caps,sample.caps)))revoke();
    const bool continuous=state.active.received && state.active.config==sample.config &&
        state.active.link==sample.link && sample.received_ms-state.active.received_ms<=MitsubishiModeHistory::observation_gap_ms;
    if(!continuous || !el::same_request(state.active.reading,sample.reading)) {
        ++state.request_generation;state.request_since_ms=sample.received_ms;
        state.automatic_queued_utc=state.automatic_confirmed_utc=0;
    }
    if(!continuous || state.active.reading.power!=sample.reading.power || state.active.reading.mode!=sample.reading.mode) {
        state.mode_since_ms=sample.received_ms;
        state.dwell.command_dispatched(sample.received_ms);
    }
    state.dwell.observe(sample.reading.power,sample.received_ms,true);
    state.active=sample;
}
void install(const Snapshot& sample) {
    state.saved=sample.config;state.active={};revoke();observe(sample);
}
void enqueue_command(Command cmd) {
    cmd.queued_utc=time(nullptr);
    ++state.write_generation;
    state.pending={Task::Write,state.active.link,state.active.config.peer,false,cmd};
    state.requested={cmd.desired,cmd.queued_utc,true};state.sent={};state.confirmed={};
    state.status="write_queued";
}

// A socket exists only during a serialized job. The fixed receive buffer
// detects overlong datagrams. Bind to STA, not the open setup access point.
// ECHONET is plaintext LAN traffic, not cryptographic device authentication.
class Session {
    int fd_=-1;
    Link link_;
    bool multicast_ready_=false;
    // Both packet buffers and parsed property views live in this heap-owned
    // per-job session, not in the 8 KiB shared LAN task stack.
    el::Packet tx_{};
    el::Frame request_{},response_{};
    std::array<el::Byte,el::max_packet+1> rx_{};
public:
    explicit Session(Link link):link_(link){}
    ~Session(){if(fd_>=0)close(fd_);}
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    bool open() {
        if(!link_.current())return false;
        fd_=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(fd_<0)return false;
        sockaddr_in address{};address.sin_family=AF_INET;
        address.sin_port=htons(el::port);
        address.sin_addr.s_addr=static_cast<uint32_t>(WiFi.localIP());
        if(!address.sin_addr.s_addr ||
           bind(fd_,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0)return false;
        in_addr iface{};iface.s_addr=address.sin_addr.s_addr;
        multicast_ready_=setsockopt(fd_,IPPROTO_IP,IP_MULTICAST_IF,&iface,sizeof(iface))==0;
        return true; // manual unicast remains usable if multicast is unavailable
    }
    static uint16_t tid() {
        // Called only on the one LAN worker. Random initial offset; a new TID
        // for every datagram, including single-property fallbacks/readback.
        static uint16_t counter=static_cast<uint16_t>(esp_random());
        return ++counter;
    }
    bool prepare_get(el::Object object,const el::Byte* codes,size_t count) {
        return el::get_into(tx_,tid(),object,codes,count) &&
            el::parse_into(request_,tx_.bytes.data(),tx_.size);
    }
    bool prepare_state(el::Object object,const el::Capabilities& caps) {
        return el::read_state_into(tx_,tid(),object,caps) &&
            el::parse_into(request_,tx_.bytes.data(),tx_.size);
    }
    bool prepare_command(el::Object object,const el::Capabilities& caps,
                         const el::ApprovedModes& approval,const el::State& current,
                         const el::Desired& desired) {
        return el::approved_command_into(tx_,tid(),object,caps,approval,current,desired) &&
            el::parse_into(request_,tx_.bytes.data(),tx_.size);
    }
    const el::Frame& request()const{return request_;}
    bool send(uint32_t ip,bool discovery=false) {
        if(fd_<0 || !link_.current() || !tx_.size || tx_.size>el::max_packet)return false;
        const uint32_t multicast=static_cast<uint32_t>(IPAddress(224,0,23,0));
        if(discovery?ip!=multicast:!local_peer(ip))return false;
        sockaddr_in peer{};peer.sin_family=AF_INET;peer.sin_port=htons(el::port);peer.sin_addr.s_addr=ip;
        return sendto(fd_,tx_.bytes.data(),tx_.size,MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&peer),sizeof(peer))
            ==static_cast<int>(tx_.size);
    }
    const el::Frame* receive(uint32_t expected_ip,uint32_t start,uint32_t timeout,uint32_t& actual_ip) {
        while(link_.current() && millis()-start<timeout) {
            sockaddr_in peer{};socklen_t size=sizeof(peer);
            const int n=recvfrom(fd_,rx_.data(),rx_.size(),MSG_DONTWAIT,
                reinterpret_cast<sockaddr*>(&peer),&size);
            if(n<0){vTaskDelay(pdMS_TO_TICKS(15));continue;}
            if(peer.sin_family!=AF_INET || !el::response_peer(expected_ip,peer.sin_addr.s_addr,
               ntohs(peer.sin_port),millis()-start,timeout,link_.current()) ||
               !local_peer(peer.sin_addr.s_addr) || n>static_cast<int>(el::max_packet))continue;
            if(!el::parse_into(response_,rx_.data(),static_cast<size_t>(n)) ||
               !el::matches(request_,response_))continue;
            actual_ip=peer.sin_addr.s_addr;return &response_;
        }
        return nullptr;
    }
    // A reply borrows the session buffers; consume it before the next query.
    const el::Frame* reply(uint32_t ip,uint32_t timeout) {
        uint32_t actual=0;return receive(ip,millis(),timeout,actual);
    }
    const el::Frame* query(uint32_t ip,el::Object obj,const el::Byte* codes,size_t count,uint32_t timeout) {
        if(!timeout || !prepare_get(obj,codes,count) || !send(ip))return nullptr;
        return reply(ip,timeout);
    }
    std::optional<el::CandidateList> scan() {
        el::CandidateList out{};
        const el::Byte code=0xd6;
        if(!multicast_ready_ || !prepare_get(el::node_profile,&code,1) ||
           !send(static_cast<uint32_t>(IPAddress(224,0,23,0)),true))return {};
        const auto start=millis();
        uint32_t actual=0;
        while(const auto* response=receive(0,start,scan_budget,actual)) {
            const auto list=el::discovery_result(request_,*response);
            if(list)for(size_t i=0;i<list->count;++i)out.add({actual,list->objects[i][2]});
        }
        return out;
    }
};

struct Poll {Snapshot sample{};const char* status="connection_failed";};
__attribute__((noinline)) Poll poll(Session& session,const Config& expected,const Link& link) {
    Poll result;
    if(!expected.peer.valid() || !local_peer(expected.peer.ipv4) || !link.current())return result;
    const auto peer=expected.peer;
    const auto start=millis();
    const auto query=[&](el::Object object,const el::Byte* codes,size_t n,uint32_t timeout=900) {
        const uint32_t used=millis()-start;
        return session.query(peer.ipv4,object,codes,n,used<poll_budget?std::min(timeout,poll_budget-used):0);
    };
    const auto one=[&](el::Object object,el::Byte code,uint32_t timeout=900) {
        return query(object,&code,1,timeout);
    };
    const auto node_id=[&]()->std::optional<el::Identity>{
        const auto f=one(el::node_profile,0x83);
        return f?el::decode_identity(f->find(0x83)):std::nullopt;
    };
    const auto id=node_id();
    if(!id){result.status="identity_unavailable";return result;}
    if(expected.valid() && !(expected.identity==*id)){result.status="device_changed";return result;}
    // Individual GET is mandatory-compatible with devices that reject multi-EPC.
    const auto map=[&](el::Byte code)->std::optional<el::PropertyMap>{
        const auto f=one(peer.object(),code);
        if(!f)return {};
        const auto* p=f->find(code);
        return p?el::property_map(*p):std::nullopt;
    };
    const auto readable=map(0x9f),writable=map(0x9e);
    if(!readable || !writable || !readable->has(0x80) || !readable->has(0xb0)) {
        result.status="unsupported";return result;
    }
    el::Capabilities caps{*readable,*writable};
    const uint32_t received_ms=millis();
    const int64_t received_utc=time(nullptr);
    std::optional<el::State> reading;
    {
        const uint32_t used=millis()-start;
        if(used<poll_budget && session.prepare_state(peer.object(),caps) && session.send(peer.ipv4)) {
            const auto* res=session.reply(peer.ipv4,std::min(uint32_t(450),poll_budget-used));
            if(res)reading=el::state(session.request(),*res);
        }
    }
    if(!reading) {
        // Some original modules accept only OPC=1, and optional EPCs may
        // return SNA. Do not let an optional sensor prevent core state reads.
        const auto core=[&]()->std::optional<el::State>{
            const auto power=one(peer.object(),0x80,450);
            if(!power)return {};
            const auto* p=power->find(0x80);
            if(!p || p->size!=1)return {};
            const auto value=p->data[0];
            const el::Property copied{0x80,&value,1};
            const auto mode=one(peer.object(),0xb0,450);
            return mode?el::operation_state(&copied,mode->find(0xb0)):std::nullopt;
        };
        reading=core();
        if(!reading){result.status="state_unavailable";return result;}
        for(const auto code:std::array<el::Byte,7>{0xb3,0xbb,0xbe,0xba,0xa0,0x88,0xaa})
            if(caps.readable.has(code))
                if(const auto f=one(peer.object(),code,250))el::optional_state(*reading,*f);
        const auto again=core();
        if(!again || again->power!=reading->power || again->mode!=reading->mode) {
            result.status="external_change";return result;
        }
        // If the device advertises a target, a second matching read prevents
        // an inconsistent target from being treated as a confirmed command.
        if(reading->setpoint_c) {
            const auto f=one(peer.object(),0xb3,350);
            el::State target{};
            if(f)el::optional_state(target,*f);
            if(target.setpoint_c!=reading->setpoint_c)reading->setpoint_c.reset();
        }
    }
    // Verify identity again before publishing; a reassigned IPv4 address
    // must not pair readings from a different node to an existing approval.
    const auto final_id=node_id();
    if(!final_id || !(*final_id==*id)){result.status="device_changed";return result;}
    result.sample={{schema,peer,*id},caps,*reading,link,received_ms,received_utc,link.current()};
    result.status=result.sample.received?"ok":"link_changed";
    return result;
}
bool command_current(const Work& job) {
    if(!job.link.current() || job.command.revision!=revision.load() || !cottage_echonet_selected() ||
       cottage_ota_confirmation_pending())return false;
    const auto& c=job.command;
    return c.automatic?cottage_echonet_automatic_write_current(c.authorization,c.config,c.settings,c.proposal):
        !cottage_echonet_automatic_enabled();
}
struct WriteResult {
    Snapshot sample{};
    const char* status="write_cancelled";
    bool observed=false,confirmed=false,attempted=false;
};
__attribute__((noinline)) WriteResult write_command(Session& session,const Work& job,const Config& config) {
    WriteResult result;
    if(!command_current(job))return result;
    auto before=poll(session,config,job.link);
    result.observed=true;result.sample=before.sample;
    if(!before.sample.received){result.status=before.status;return result;}
    const auto& desired=job.command.desired;
    xSemaphoreTake(gate,portMAX_DELAY);
    observe(before.sample);
    const bool mode_allowed=!el::mode_change(before.sample.reading,desired) ||
        state.dwell.since_change(millis())>=mitsubishi_min_mode_dwell_ms;
    const auto approval=state.approval.modes;
    const bool permitted=approved() && job.command.revision==revision.load();
    xSemaphoreGive(gate);
    if(!permitted)return result;
    if(!el::same_request(before.sample.reading,job.command.previous)) {
        result.status="external_change";return result;
    }
    if(el::applied(before.sample.reading,desired,before.sample.reading)) {
        result.status="already_applied";return result;
    }
    if(!session.prepare_command(config.peer.object(),before.sample.caps,
                                approval,before.sample.reading,desired)) {
        result.status="unsupported_command";return result;
    }
    if(!mode_allowed){result.status="mode_dwell";return result;}
    if(!command_current(job))return result;
    xSemaphoreTake(gate,portMAX_DELAY);
    if(!approved() || job.command.revision!=revision.load()) {
        xSemaphoreGive(gate);return result;
    }
    state.sent={desired,time(nullptr),true};
    if(el::mode_change(before.sample.reading,desired))state.dwell.command_dispatched(millis());
    xSemaphoreGive(gate);
    if(!command_current(job)) {
        xSemaphoreTake(gate,portMAX_DELAY);state.sent={};xSemaphoreGive(gate);
        return result;
    }
    const bool dispatched=session.send(config.peer.ipv4);
    result.attempted=dispatched;
    if(!dispatched){result.status="send_failed";return result;}
    const auto ack=session.reply(config.peer.ipv4,700);
    result.status=ack?"unconfirmed":"send_failed";
    // A Set_Res is acceptance only. Never resend a SET, even after a timeout.
    result.sample={};
    for(unsigned attempt=0;attempt<2 && job.link.current();++attempt) {
        vTaskDelay(pdMS_TO_TICKS(350));
        auto after=poll(session,config,job.link);
        if(!after.sample.received){result.sample={};result.status=after.status;if(after.status==std::string_view("device_changed"))break;continue;}
        result.sample=after.sample;result.status="unconfirmed";
        if(!el::same_caps(before.sample.caps,after.sample.caps)) {result.status="capabilities_changed";break;}
        if(el::applied(before.sample.reading,desired,after.sample.reading) && command_current(job)) {
            result.confirmed=true;result.status="confirmed";break;
        }
    }
    return result;
}
bool web_ready(WebServer& web,bool write) {
    if(!cottage_wifi_require_home_lan(web))return false;
    if(!gate){web.send(503,"application/json","{}");return false;}
    if(write && (!web.hasArg("token") || !c23_authorize_local_setting(web.arg("token").c_str()))) {
        web.send(403,"application/json","{\"error\":\"authorization\"}");return false;
    }
    return true;
}
bool unsigned_number(const String& value,unsigned min,unsigned max,unsigned& out) {
    if(value.isEmpty() || value.length()>3)return false;
    unsigned n=0;
    for(size_t i=0;i<value.length();++i){const auto c=value[i];if(c<'0'||c>'9')return false;n=n*10+c-'0';}
    if(n<min || n>max)return false;out=n;return true;
}
bool parse_range(WebServer& web,const char* enabled,const char* min,const char* max,el::ModeRange& range) {
    const String requested=web.arg(enabled);
    if(requested=="false"){range={};return true;}
    unsigned a=0,b=0;
    if(requested!="true" || !unsigned_number(web.arg(min),0,50,a) ||
       !unsigned_number(web.arg(max),0,50,b) || a>=b)return false;
    range={static_cast<int>(a),static_cast<int>(b),true};return true;
}
void add_receipt(cJSON* root,const char* key,const Receipt& r) {
    if(!r.present)return;
    auto* item=cJSON_AddObjectToObject(root,key);
    if(!item)return;
    cJSON_AddNumberToObject(item,"utc",r.utc);
    if(r.desired.power)cJSON_AddBoolToObject(item,"power",*r.desired.power);
    if(r.desired.mode)cJSON_AddNumberToObject(item,"mode",static_cast<unsigned>(*r.desired.mode));
    if(r.desired.setpoint_c)cJSON_AddNumberToObject(item,"target_c",*r.desired.setpoint_c);
}
void status(WebServer& web) {
    const bool draft=web.arg("draft")=="true";
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool staged=draft && state.stage_id && state.staged.link.current();
    const bool show_active=!draft || (!state.draft_started && cottage_echonet_selected());
    const auto sample=staged?state.staged:show_active?state.active:Snapshot{};
    const auto saved=staged?state.staged.config:show_active?state.saved:Config{};
    const auto candidates=state.candidates;
    const bool scan_current=state.scan_received && state.scan_link.current() && millis()-state.scan_ms<=scan_freshness;
    const bool busy=state.busy || state.pending.task!=Task::None;
    const auto stage_id=staged?state.stage_id:0;
    const bool pilot=!draft && cottage_echonet_selected() && approved();
    const bool write_available=pilot && available() && !cottage_echonet_automatic_enabled();
    const auto approval=state.approval;
    const auto requested=state.requested,sent=state.sent,confirmed=state.confirmed;
    const bool receipts_current=pilot && approval.link.current();
    const auto rev=revision.load();
    const char* result=draft?state.draft_status:state.status;
    xSemaphoreGive(gate);
    auto* root=cJSON_CreateObject();
    if(!root){web.send(503,"application/json","{}");return;}
    const bool current=fresh(sample);
    cJSON_AddBoolToObject(root,"configured",saved.valid());
    cJSON_AddBoolToObject(root,"fresh",current);
    cJSON_AddBoolToObject(root,"busy",busy);
    cJSON_AddStringToObject(root,"result",result);
    if(stage_id)cJSON_AddNumberToObject(root,"draft_id",stage_id);
    if(saved.valid()) {
        cJSON_AddStringToObject(root,"host",IPAddress(saved.peer.ipv4).toString().c_str());
        cJSON_AddNumberToObject(root,"instance",saved.peer.instance);
        cJSON_AddStringToObject(root,"device",peer_name(saved.peer).c_str());
    }
    auto* list=cJSON_AddArrayToObject(root,"candidates");
    if(list && scan_current)for(size_t i=0;i<candidates.count;++i) {
        auto* item=cJSON_CreateObject();if(!item)break;
        cJSON_AddStringToObject(item,"host",IPAddress(candidates.items[i].ipv4).toString().c_str());
        cJSON_AddNumberToObject(item,"instance",candidates.items[i].instance);
        cJSON_AddItemToArray(list,item);
    }
    cJSON_AddBoolToObject(root,"scan_truncated",scan_current && candidates.truncated);
    if(current) {
        cJSON_AddBoolToObject(root,"power",sample.reading.power);
        cJSON_AddNumberToObject(root,"mode",static_cast<unsigned>(sample.reading.mode));
        cJSON_AddBoolToObject(root,"control_properties",el::controllable(sample.caps));
        if(sample.reading.setpoint_c)cJSON_AddNumberToObject(root,"target_c",*sample.reading.setpoint_c);
        if(sample.reading.indoor_c)cJSON_AddNumberToObject(root,"indoor_c",*sample.reading.indoor_c);
        if(sample.reading.outdoor_c)cJSON_AddNumberToObject(root,"outdoor_c",*sample.reading.outdoor_c);
        if(sample.reading.humidity_pct)cJSON_AddNumberToObject(root,"humidity_pct",*sample.reading.humidity_pct);
        if(sample.reading.fault)cJSON_AddBoolToObject(root,"fault",*sample.reading.fault);
    }
    if(!draft) {
        cJSON_AddNumberToObject(root,"revision",rev);
        cJSON_AddBoolToObject(root,"pilot_approved",pilot);
        cJSON_AddBoolToObject(root,"heat_approved",pilot && approval.modes.heat.valid());
        cJSON_AddBoolToObject(root,"cool_approved",pilot && approval.modes.cool.valid());
        cJSON_AddBoolToObject(root,"write_available",write_available);
        if(pilot) {
            cJSON_AddStringToObject(root,"pilot_model",approval.model);
            if(approval.modes.heat.valid()) {
                cJSON_AddNumberToObject(root,"heat_min",approval.modes.heat.min_c);
                cJSON_AddNumberToObject(root,"heat_max",approval.modes.heat.max_c);
            }
            if(approval.modes.cool.valid()) {
                cJSON_AddNumberToObject(root,"cool_min",approval.modes.cool.min_c);
                cJSON_AddNumberToObject(root,"cool_max",approval.modes.cool.max_c);
            }
        }
        if(receipts_current){add_receipt(root,"requested",requested);add_receipt(root,"sent",sent);add_receipt(root,"confirmed",confirmed);}
    }
    char* data=cJSON_PrintUnformatted(root);
    web.send_P(data?200:503,"application/json",data?data:"{}");cJSON_free(data);cJSON_Delete(root);
}
void enqueue(WebServer& web,Task task,el::Peer peer={}) {
    const bool draft=web.arg("draft")=="true";
    const bool eligible=draft || cottage_echonet_selected();
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool ok=eligible && live_link().current() && !state.busy && state.pending.task==Task::None;
    if(ok) {
        state.pending={task,live_link(),peer,draft,{}};
        if(task==Task::Scan){state.scan_received=false;state.candidates={};}
        if(task==Task::Identify && draft){state.draft_started=true;state.staged={};state.stage_id=0;}
    }
    xSemaphoreGive(gate);
    web.send(ok?202:409,"application/json",ok?"{\"queued\":true}":"{\"error\":\"busy_or_link\"}");
}
} // namespace

bool cottage_echonet_selected(){return std::strcmp(site_settings_snapshot().pump_adapter,adapter_id)==0;}
bool cottage_echonet_needs_worker() {
    if(!gate)return false;
    if(cottage_echonet_selected())return true;
    xSemaphoreTake(gate,portMAX_DELAY);
    const bool pending=state.pending.task!=Task::None;
    xSemaphoreGive(gate);return pending;
}
void cottage_echonet_register_routes(WebServer& web) {
    gate=xSemaphoreCreateMutex();if(!gate)return;
    Preferences p;Config stored{};
    if(p.begin("cottage_echo",true) && p.getBytesLength("peer1")==sizeof(stored) &&
       p.getBytes("peer1",&stored,sizeof(stored))==sizeof(stored) && stored.valid())state.saved=stored;
    web.on("/api/echonet/status",HTTP_GET,[&web](){if(web_ready(web,false))status(web);});
    web.on("/api/echonet/discover",HTTP_POST,[&web](){if(web_ready(web,true))enqueue(web,Task::Scan);});
    web.on("/api/echonet/identify",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        IPAddress ip;unsigned instance=0;
        if(!local_http_peer(web.arg("host").c_str(),ip) ||
           !unsigned_number(web.arg("instance"),1,127,instance)) {
            web.send(400,"application/json","{\"error\":\"invalid_address\"}");return;
        }
        enqueue(web,Task::Identify,{static_cast<uint32_t>(ip),static_cast<el::Byte>(instance)});
    });
    web.on("/api/echonet/test",HTTP_POST,[&web](){if(web_ready(web,true))enqueue(web,Task::Read);});
    web.on("/api/echonet/commit",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool matching=state.stage_id && web.arg("draft_id")==String(state.stage_id) &&
            fresh(state.staged) && !state.busy && state.pending.task==Task::None;
        const bool saved=matching && persist(state.staged.config);
        if(saved){install(state.staged);state.staged={};state.stage_id=0;state.draft_started=false;state.status="ok";}
        xSemaphoreGive(gate);
        web.send(saved?200:matching?500:409,"application/json",saved?"{\"saved\":true}":
            matching?"{\"error\":\"storage\"}":"{\"error\":\"connection_failed\"}");
    });
    web.on("/api/echonet/pilot",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const auto enabled=web.arg("enabled"),model=web.arg("model");
        const bool requested=enabled=="true";
        el::ApprovedModes modes{};
        if((!requested && enabled!="false") || (requested &&
           (!parse_range(web,"heat","heat_min","heat_max",modes.heat) ||
            !parse_range(web,"cool","cool_min","cool_max",modes.cool) || !modes.valid() ||
            model.length()<3 || model.length()>47))) {
            web.send(400,"application/json","{\"error\":\"model_limits\"}");return;
        }
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool allowed=cottage_echonet_selected() && (!requested ||
            (web.arg("revision")==String(revision.load()) && web.arg("device")==peer_name(state.active.config.peer) &&
             fresh(state.active) && !state.busy && state.pending.task==Task::None &&
             el::controllable(state.active.caps) && state.active.reading.fault!=true &&
             !cottage_ota_confirmation_pending()));
        if(allowed) {
            revoke();
            if(requested) {
                state.approval={modes,state.active.config,state.active.caps,state.active.link,revision.load(),{}};
                strlcpy(state.approval.model,model.c_str(),sizeof(state.approval.model));
            }
            state.status=requested?"pilot_approved":"ok";
        }
        xSemaphoreGive(gate);
        web.send(allowed?200:409,"application/json",allowed?"{\"saved\":true}":"{\"error\":\"pilot_required\"}");
    });
    web.on("/api/echonet/control",HTTP_POST,[&web](){
        if(!web_ready(web,true))return;
        const auto action=web.arg("action");
        el::Desired desired{};
        unsigned target=0;
        if(action=="off")desired.power=false;
        else if((action=="heat" || action=="cool" || action=="target") &&
                unsigned_number(web.arg("target"),0,50,target)) {
            desired.setpoint_c=static_cast<int>(target);
            if(action!="target"){desired.power=true;desired.mode=action=="heat"?el::Mode::Heat:el::Mode::Cool;}
        } else {web.send(400,"application/json","{\"error\":\"unsupported_command\"}");return;}
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool ready=cottage_echonet_selected() && !cottage_echonet_automatic_enabled() &&
            !cottage_ota_confirmation_pending() && web.arg("revision")==String(revision.load()) && available();
        const bool supported=ready && el::approved_limits(state.active.caps,state.approval.modes,state.active.reading,desired).has_value();
        const bool allowed=supported && (!el::mode_change(state.active.reading,desired) ||
            state.dwell.since_change(millis())>=mitsubishi_min_mode_dwell_ms);
        if(allowed) {
            Command cmd{};cmd.desired=desired;cmd.previous=state.active.reading;cmd.revision=revision.load();enqueue_command(cmd);
        }
        xSemaphoreGive(gate);
        web.send(allowed?202:409,"application/json",allowed?"{\"queued\":true}":
            !ready?"{\"error\":\"pilot_required\"}":!supported?"{\"error\":\"unsupported_command\"}":
            "{\"error\":\"mode_dwell\"}");
    });
}

uint32_t cottage_echonet_configuration_generation(){return revision.load();}
uint32_t cottage_echonet_calibration_identity() {
    if(!gate)return 0;
    xSemaphoreTake(gate,portMAX_DELAY);const auto config=state.saved;xSemaphoreGive(gate);
    if(!config.valid())return 0;
    uint32_t hash=2166136261u;
    for(size_t i=0;i<config.identity.size;++i){hash^=config.identity.bytes[i];hash*=16777619u;}
    hash^=config.peer.instance;hash*=16777619u;
    return hash?hash:1;
}
bool cottage_echonet_automatic_ready() {
    if(!gate || !cottage_echonet_selected())return false;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto mode=el::room_mode(state.active.reading);
    const bool ready=available() && state.approval.modes.heat.valid() &&
        (mode==PumpRoomMode::Heat || mode==PumpRoomMode::Off);
    xSemaphoreGive(gate);return ready;
}
ControlBlock cottage_echonet_submit_automatic(const PumpRoomState& earlier,ControlSnapshot proposal,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    if(!gate || !cottage_echonet_automatic_enabled())return ControlBlock::Disabled;
    xSemaphoreTake(gate,portMAX_DELAY);
    ControlBlock result=ControlBlock::InputsUnverified;
    const auto mode=el::room_mode(state.active.reading);
    if(state.reconciliation.awaiting(authorization)) {
        xSemaphoreGive(gate);return ControlBlock::AwaitingReadback;
    }
    if(available() && state.approval.modes.heat.valid() &&
       earlier.configuration_generation==revision.load() && earlier.endpoint_generation==state.active.link.endpoint &&
       earlier.link_generation==state.active.link.wifi && earlier.request_generation==state.request_generation &&
       (mode==PumpRoomMode::Off || mode==PumpRoomMode::Heat)) {
        const auto initial=proposal.proposal;
        const auto limits=state.approval.modes.heating();
        proposal.current_heat=mode==PumpRoomMode::Heat;
        proposal.current_setpoint_c=state.active.reading.setpoint_c?double(*state.active.reading.setpoint_c):NAN;
        proposal.cn105_link_fresh=proposal.bridge_get_ok=true;
        proposal.ms_since_mode_change=state.dwell.since_change(millis());
        const auto decision=choose_pump_control(proposal,limits,HeatTransition::Combined);
        result=decision.gate;
        if(decision.step!=ControlStep::None) {
            Command cmd{};
            cmd.previous=state.active.reading;cmd.proposal=initial;cmd.automatic=true;cmd.revision=revision.load();
            cmd.authorization=authorization;cmd.config=config;cmd.settings=settings;
            if(decision.step==ControlStep::SetOff)cmd.desired.power=false;
            else {
                cmd.desired.setpoint_c=static_cast<int>(decision.target_c);
                if(decision.step==ControlStep::SetHeat){cmd.desired.power=true;cmd.desired.mode=el::Mode::Heat;}
            }
            if(el::approved_limits(state.active.caps,state.approval.modes,state.active.reading,cmd.desired))enqueue_command(cmd);
            else result=ControlBlock::UnsupportedCommand;
        }
    }
    xSemaphoreGive(gate);return result;
}
PumpRoomState cottage_echonet_room_state() {
    PumpRoomState out{};
    if(!gate || !cottage_echonet_selected())return out;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto sample=state.active;
    out.configuration_generation=revision.load();
    if(approved())out.heating=state.approval.modes.heating();
    out.request_generation=state.request_generation;
    out.command_unconfirmed=state.reconciliation.awaiting(cottage_control_authorization_generation());
    out.write_generation=state.write_generation;
    out.write_in_progress=state.write_active || state.pending.task==Task::Write;
    out.request_minutes=(millis()-state.request_since_ms)/60000;
    out.mode_minutes=(millis()-state.mode_since_ms)/60000;
    out.automatic_queued_utc=state.automatic_queued_utc;out.automatic_confirmed_utc=state.automatic_confirmed_utc;
    xSemaphoreGive(gate);
    if(!fresh(sample))return out;
    out.protocol=PumpRoomProtocol::EchonetLite;out.source=PumpStateSource::DirectLan;out.type=HeatPumpType::AirToAir;
    out.endpoint_generation=sample.link.endpoint;out.link_generation=sample.link.wifi;
    out.mode=el::room_mode(sample.reading);
    if(sample.reading.setpoint_c)out.setpoint_c=*sample.reading.setpoint_c;
    if(sample.reading.indoor_c)out.indoor_c=*sample.reading.indoor_c;
    if(sample.reading.outdoor_c)out.outdoor_c=*sample.reading.outdoor_c;
    if(sample.received_utc>=1700000000)out.evidence={sample.received_utc,0,false};
    return out;
}
void cottage_echonet_tick() {
    if(!gate || WiFi.status()!=WL_CONNECTED || cottage_ota_network_maintenance())return;
    static uint32_t last=0;
    static Link last_link{};
    const auto link=live_link();
    const bool selected=cottage_echonet_selected();
    xSemaphoreTake(gate,portMAX_DELAY);
    auto job=state.pending;
    if(job.task==Task::None && selected && state.saved.valid() &&
       (!(link==last_link) || millis()-last>=poll_interval))job={Task::Read,link,{},{},{}};
    const bool staged=job.draft && state.stage_id && state.staged.link.current();
    Config config=staged?state.staged.config:state.saved;
    state.pending={};state.busy=job.task!=Task::None;
    state.write_active=job.task==Task::Write;
    xSemaphoreGive(gate);
    if(job.task==Task::None)return;
    if(job.task==Task::Identify)config={0,job.peer,{}};
    Poll result{};
    WriteResult command{};
    el::CandidateList candidates{};
    bool scanned=false;
    if(job.link.current() && (job.draft || selected)) {
        enevina_firmware::NetworkBudgetLease lease(700,72*1024,16*1024);
        if(lease.held()) {
            std::unique_ptr<Session> session(new(std::nothrow) Session(job.link));
            if(session && session->open()) {
                if(job.task==Task::Scan){
                    const auto found=session->scan();
                    scanned=found.has_value() && job.link.current();
                    if(scanned)candidates=*found;
                    result.status=scanned?"scan_complete":!job.link.current()?"link_changed":"socket_failed";
                }
                else if(job.task==Task::Write)command=write_command(*session,job,config);
                else result=poll(*session,config,job.link);
            } else {result.status="socket_failed";command.status="socket_failed";}
        } else {result.status="network_busy";command.status="network_busy";}
    }
    xSemaphoreTake(gate,portMAX_DELAY);state.busy=false;
    state.write_active=false;
    if(job.task==Task::Write && job.command.automatic && command.attempted &&
       !command.confirmed && job.command.revision==revision.load()) {
        const auto& wanted=job.command.desired;
        state.automatic_queued_utc=state.automatic_confirmed_utc=0;
        ++state.request_generation;state.request_since_ms=millis();
        state.reconciliation.unresolved(job.command.authorization,
            wanted.power==false?PumpRoomMode::Off:PumpRoomMode::Heat,
            wanted.setpoint_c.has_value(),wanted.setpoint_c.value_or(0));
    }
    if(job.link.current() && (job.draft || cottage_echonet_selected())) {
        if(job.task==Task::Scan) {
            if(scanned){state.candidates=candidates;state.scan_link=job.link;state.scan_ms=millis();state.scan_received=true;}
        } else if(job.task==Task::Write) {
            result.status=command.status;
            if(job.command.revision==revision.load()) {
                if(!command.sample.received && std::strcmp(command.status,"device_changed")==0)revoke();
                if(command.observed)observe(command.sample);
                if(command.confirmed && approved() && job.command.revision==revision.load()) {
                    state.confirmed={job.command.desired,command.sample.received_utc,true};
                    if(job.command.automatic) {
                        state.automatic_queued_utc=job.command.queued_utc;
                        state.automatic_confirmed_utc=command.sample.received_utc;
                    }
                }
            }
        } else if(job.task==Task::Identify && result.sample.received) {
            if(job.draft) {
                if(++state.next_id==0)++state.next_id;
                state.staged=result.sample;state.stage_id=state.next_id;
            } else if(persist(result.sample.config)) {
                install(result.sample);state.staged={};state.stage_id=0;state.draft_started=false;
            } else result.status="storage";
        } else if(job.task==Task::Read) {
            if(staged) {
                if(result.sample.received)state.staged=result.sample;else state.staged.received=false;
            } else if(!job.draft || !state.draft_started) {
                if(!result.sample.received && std::strcmp(result.status,"device_changed")==0)revoke();
                observe(result.sample);
                if(!job.draft && selected && state.reconciliation.observe(
                        cottage_control_authorization_generation(),available(),
                        el::room_mode(state.active.reading),
                        state.active.reading.setpoint_c?double(*state.active.reading.setpoint_c):NAN)) {
                    ++state.request_generation;state.request_since_ms=millis();
                    state.automatic_queued_utc=state.automatic_confirmed_utc=0;
                }
            }
        }
    } else result.status="link_changed";
    (job.draft?state.draft_status:state.status)=result.status;
    xSemaphoreGive(gate);
    if(!job.draft && job.task!=Task::Scan){last=millis();last_link=job.link;}
}
#endif
