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
#include <array>
#include <cstring>
#include <ctime>

namespace {
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
enum class Work { None, Scan, Pair, Read };
struct Job {
    Work work=Work::None;
    Config proposed{};
    Link link{};
};
struct State {
    Config saved{};
    uint32_t revision=0;
    Sample sample{};
    std::array<Candidate,max_candidates> candidates{};
    size_t candidate_count=0;
    Link scan_link{};
    Job queued{};
    bool busy=false;
    const char* result="not_tested";
};

SemaphoreHandle_t guard=nullptr;
State state{};
Link active_link() {
    return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};
}
bool configured(const Config& c) {return c.version==config_version;}
bool fresh(const Sample& s,uint32_t revision) {
    return s.reading && s.link.current() && s.config==revision &&
        static_cast<uint32_t>(millis()-s.received_ms)<=fresh_s*1000;
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

void status(WebServer& web) {
    xSemaphoreTake(guard,portMAX_DELAY);
    const State s=state;
    xSemaphoreGive(guard);
    const bool is_fresh=fresh(s.sample,s.revision);
    auto* root=cJSON_CreateObject();
    if(!root) {web.send(503,"application/json","{}");return;}
    cJSON_AddBoolToObject(root,"configured",configured(s.saved));
    cJSON_AddBoolToObject(root,"fresh",is_fresh);
    cJSON_AddBoolToObject(root,"busy",s.busy || s.queued.work!=Work::None);
    cJSON_AddStringToObject(root,"result",s.result);
    if(configured(s.saved)) {
        cJSON_AddStringToObject(root,"host",s.saved.host);
        cJSON_AddStringToObject(root,"device",s.saved.device);
        cJSON_AddNumberToObject(root,"cipher",static_cast<unsigned>(s.saved.cipher));
    }
    if(is_fresh) {
        const auto& r=*s.sample.reading;
        cJSON_AddBoolToObject(root,"power",r.power);
        cJSON_AddNumberToObject(root,"mode",static_cast<unsigned>(r.mode));
        if(r.setpoint_c)cJSON_AddNumberToObject(root,"target_c",*r.setpoint_c);
        if(r.indoor_c)cJSON_AddNumberToObject(root,"indoor_c",*r.indoor_c);
        if(r.frost)cJSON_AddBoolToObject(root,"frost",*r.frost);
        if(s.sample.utc>=1700000000)cJSON_AddNumberToObject(root,"read_utc",s.sample.utc);
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
    if(!pending)state.queued={work,proposed,active_link()};
    xSemaphoreGive(guard);
    web.send(pending?409:202,"application/json",pending?
        "{\"error\":\"busy\"}":"{\"queued\":true}");
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
        if(web_ready(web,false))status(web);
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
}

enevina::PumpRoomState cottage_gree_room_state() {
    using namespace enevina;
    PumpRoomState out{};
    if(!guard || !cottage_gree_selected())return out;
    xSemaphoreTake(guard,portMAX_DELAY);
    const auto s=state.sample;
    const auto revision=state.revision;
    const auto cipher=state.saved.cipher;
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
    out.mode=!r.power?PumpRoomMode::Off:r.mode==Mode::Heat?PumpRoomMode::Heat:PumpRoomMode::Other;
    if(r.setpoint_c)out.setpoint_c=*r.setpoint_c;
    if(r.indoor_c)out.indoor_c=*r.indoor_c;
    return out;
}

void cottage_gree_worker(void*) {
    uint32_t last_poll=0,last_wifi=0,last_endpoint=0;
    for(;;) {
        if(WiFi.status()!=WL_CONNECTED || cottage_ota_network_maintenance()) {
            vTaskDelay(pdMS_TO_TICKS(400));
            continue;
        }
        const auto link=active_link();
        const bool selected=cottage_gree_selected();
        xSemaphoreTake(guard,portMAX_DELAY);
        Job job=state.queued;
        if(job.work==Work::None && selected && configured(state.saved) &&
           (millis()-last_poll>=poll_ms || last_wifi!=link.wifi || last_endpoint!=link.endpoint))
            job={Work::Read,{},link};
        const Config old=state.saved;
        state.queued={};
        state.busy=job.work!=Work::None;
        xSemaphoreGive(guard);
        if(job.work==Work::None) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        const char* result="link_changed";
        Config candidate=job.proposed;
        std::optional<Reading> reading;
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
                if(store_config(candidate)) {
                    state.saved=candidate;
                    ++state.revision;
                    state.sample={reading,time(nullptr),millis(),job.link,state.revision};
                } else result="store_failed";
            } else if(job.work==Work::Read) {
                state.sample={reading,time(nullptr),millis(),job.link,state.revision};
            }
            state.result=result;
        } else state.result="link_changed";
        xSemaphoreGive(guard);
        if(job.work!=Work::Scan) {
            last_poll=millis();last_wifi=job.link.wifi;last_endpoint=job.link.endpoint;
        }
    }
}
#endif
