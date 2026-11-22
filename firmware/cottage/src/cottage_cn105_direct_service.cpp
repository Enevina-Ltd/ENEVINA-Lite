#ifndef COTTAGE_BENCH_MODE
#include "cottage_cn105_direct_service.hpp"
#include "cottage_air_service.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "pump/cn105_uart.hpp"

#include <HardwareSerial.h>
#include <Preferences.h>
#include <WebServer.h>
#include <cJSON.h>
#include <atomic>
#include <cstring>
#include <ctime>

namespace {
using namespace enevina;
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
struct Job {
    uint32_t baud=0;
    Link link{};
    bool draft=false;
};
struct State {
    uint32_t allowed_baud=0,revision=0,draft_id=0,next_id=0,request_generation=0;
    Sample active{},staged{};
    Job job{};
    bool busy=false;
    uint32_t request_since=0,mode_since=0;
    const char* status="not_tested";
    const char* staged_status="not_tested";
};
HardwareSerial uart(1);
SemaphoreHandle_t gate=nullptr;
State state{};
std::atomic<bool> uart_active{false};
uint32_t active_baud=0;
std::atomic<uint32_t> bus_generation{0};
Link active_link{};

bool valid_baud(uint32_t baud) {return baud==2400 || baud==9600;}
Link live_link() {return {site_settings_endpoint_generation(),cottage_wifi_link_generation()};}
bool fresh(const Sample& sample) {
    return sample.received && sample.link.current() && millis()-sample.received_ms<=valid_age;
}
bool store_baud(uint32_t baud) {
    Preferences p;
    return valid_baud(baud) && p.begin("cottage_cn1",false) &&
        p.putUInt("baud",baud)==sizeof(baud) && p.getUInt("baud")==baud;
}
void disconnect() {
    if(uart_active.exchange(false))uart.end();
    active_baud=0;
    ++bus_generation;
}
template<typename Accept>
std::optional<Frame> exchange(const Frame& request,const Link& link,Accept accept) {
    if(!link.current())return {};
    // Drain replies to previous transactions before sending the next request.
    for(unsigned i=0;i<128 && uart.available();++i)uart.read();
    if(uart.available() || uart.write(request.bytes.data(),request.size)!=request.size)return {};
    uart.flush(true);
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
        if(!continuous || !same_mode)state.mode_since=sample.received_ms;
        if(!continuous || !same_mode || sample.climate.target_c!=previous.target_c ||
           sample.climate.encoding!=previous.encoding) {
            state.request_since=sample.received_ms;
            ++state.request_generation;
        }
    }
    state.active=sample;
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
    const auto baud=staged?sample.baud:state.allowed_baud;
    const auto id=staged?state.draft_id:0;
    const auto result=draft?state.staged_status:state.status;
    const bool busy=state.busy || state.job.baud!=0;
    xSemaphoreGive(gate);

    auto* root=cJSON_CreateObject();
    if(!root) {web.send(503,"application/json","{}");return;}
    const bool current=fresh(sample) && (staged || sample.bus_generation==bus_generation.load());
    cJSON_AddBoolToObject(root,"configured",valid_baud(baud));
    cJSON_AddBoolToObject(root,"fresh",current);
    cJSON_AddBoolToObject(root,"busy",busy);
    cJSON_AddStringToObject(root,"result",result);
    if(id)cJSON_AddNumberToObject(root,"draft_id",id);
    if(valid_baud(baud))cJSON_AddNumberToObject(root,"baud",baud);
    if(current) {
        if(sample.climate.power)cJSON_AddBoolToObject(root,"power",*sample.climate.power);
        if(sample.climate.mode!=Mode::Unknown)
            cJSON_AddNumberToObject(root,"mode",static_cast<unsigned>(sample.climate.mode));
        if(sample.climate.target_c)cJSON_AddNumberToObject(root,"target_c",*sample.climate.target_c);
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
        const auto stored=prefs.getUInt("baud");
        if(valid_baud(stored))state.allowed_baud=stored;
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
    web.on("/api/cn105-direct/commit",HTTP_POST,[&web](){
        if(!access(web,true))return;
        const String id=web.arg("draft_id");
        xSemaphoreTake(gate,portMAX_DELAY);
        const bool matched=state.draft_id && id==String(state.draft_id) && fresh(state.staged) &&
            !state.busy && state.job.baud==0;
        const bool saved=matched && store_baud(state.staged.baud);
        if(saved) {
            state.allowed_baud=state.staged.baud;
            state.active={};observe(state.staged);
            state.staged={};state.draft_id=0;
            ++state.revision;state.status="ok";
        }
        xSemaphoreGive(gate);
        web.send(saved?200:matched?500:409,"application/json",saved?
            "{\"saved\":true}":matched?"{\"error\":\"storage\"}":"{\"error\":\"connection_failed\"}");
    });
}
uint32_t cottage_cn105_direct_configuration_generation() {
    if(!gate)return 0;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto current=state.revision;
    xSemaphoreGive(gate);
    return current;
}
uint32_t cottage_cn105_direct_calibration_identity() {
    if(!gate)return 0;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto id=state.allowed_baud;
    xSemaphoreGive(gate);
    return id;
}
PumpRoomState cottage_cn105_direct_room_state() {
    PumpRoomState out{};
    if(!gate || !cottage_cn105_direct_selected())return out;
    xSemaphoreTake(gate,portMAX_DELAY);
    const auto sample=state.active;
    out.configuration_generation=state.revision;
    out.request_generation=state.request_generation;
    out.request_minutes=(millis()-state.request_since)/60000;
    out.mode_minutes=(millis()-state.mode_since)/60000;
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
    if(!explicit_test && selected && valid_baud(state.allowed_baud) &&
       (millis()-last>=poll_interval || link.endpoint!=last_endpoint || link.wifi!=last_wifi))
        job={state.allowed_baud,link};
    state.job={};
    state.busy=job.baud!=0;
    xSemaphoreGive(gate);
    if(!job.baud) {
        if(!selected && uart_active.load())disconnect();
        return;
    }
    const auto sample=poll(job.baud,job.link);
    if(job.draft && !selected)disconnect();
    xSemaphoreTake(gate,portMAX_DELAY);
    state.busy=false;
    if(job.link.current()) {
        auto& result=job.draft?state.staged_status:state.status;
        result=sample.received?"ok":"connection_failed";
        if(job.draft) {
            if(sample.received) {
                if(++state.next_id==0)++state.next_id;
                state.draft_id=state.next_id;state.staged=sample;
            }
        } else if(explicit_test && sample.received && state.allowed_baud!=job.baud) {
            if(store_baud(job.baud)) {
                state.allowed_baud=job.baud;
                state.active={};observe(sample);
                ++state.revision;
            } else result="storage";
        } else if(!explicit_test || !sample.received || job.baud==state.allowed_baud)observe(sample);
    }
    xSemaphoreGive(gate);
    last=millis();last_endpoint=job.link.endpoint;last_wifi=job.link.wifi;
}
#endif
