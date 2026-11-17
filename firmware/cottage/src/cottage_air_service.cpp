#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Preferences.h>
#include <WebServer.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <cJSON.h>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include "cottage_config.hpp"
#include "cottage_air_service.hpp"
#include "cottage_network_budget.hpp"
#include <atomic>
#include "cottage/ruuvi_v5.hpp"
#include "cottage/air_humidity.hpp"
#include "../../main/weather.hpp"

namespace enevina::cottage {
namespace {
SemaphoreHandle_t guard=nullptr;
RuuviBinding indoor_binding,outdoor_binding;
AirHistory history;
MoistureMonitor alarm_engine;
CottageModeConfig config{};
AdvisoryLimits warning_limits{};
AirMoisture indoor_air{},outdoor_air{};
RuuviV5 last_indoor{},last_outdoor{},discovered{};
int64_t indoor_utc=0,outdoor_utc=0;
uint64_t indoor_mono_ms=0,outdoor_mono_ms=0;
uint64_t last_store_ms=0,last_tick_ms=0;
bool has_indoor=false,has_outdoor=false,have_discovered=false;
bool history_changed=false;
uint32_t samples_ok=0,samples_rejected=0,samples_duplicate=0,stores=0;
uint32_t low_heap_skips=0;
std::atomic<bool> ble_initialized{false};
char csrf_token[24]={};
struct AirEvent {
    int64_t utc=0;
    uint64_t monotonic_ms=0;
    uint8_t kind=0,code=0;
};
std::array<AirEvent,48> events{};
size_t events_next=0,events_count=0;
bool last_alert_active[moisture_alert_count]{};
constexpr uint64_t checkpoint_ms=6ULL*3600*1000;

uint64_t uptime_ms() {
    return static_cast<uint64_t>(esp_timer_get_time()/1000);
}
int64_t utc_valid_now() {
    const int64_t t=static_cast<int64_t>(time(nullptr));
    return t>1700000000?t:0;
}
void lock() {xSemaphoreTake(guard,portMAX_DELAY);}
void unlock() {xSemaphoreGive(guard);}
void log_event(uint8_t kind,uint8_t code) {
    auto& e=events[events_next];
    e.utc=utc_valid_now();e.monotonic_ms=uptime_ms();
    e.kind=kind;e.code=code;
    events_next=(events_next+1)%events.size();
    if(events_count<events.size())events_count++;
}
void refresh_alert_events() {
    for(size_t i=0;i<moisture_alert_count;i++) {
        const bool now=alarm_engine.alert(static_cast<MoistureAlertKind>(i)).active;
        if(now!=last_alert_active[i]) {
            log_event(now?1:2,static_cast<uint8_t>(i));
            last_alert_active[i]=now;
        }
    }
}
void accept_indoor(const RuuviV5& r,uint64_t ms) {
    const auto result=indoor_binding.accept(r,ms);
    if(result==RuuviAccept::Accepted) {
        const AirMoisture calculated=
            calculate_air_moisture(r.temperature_c,r.relative_humidity_pct);
        if(!calculated.valid) {samples_rejected++;return;}
        indoor_air=calculated;
        last_indoor=r;has_indoor=true;
        indoor_mono_ms=ms;indoor_utc=utc_valid_now();
        alarm_engine.observe(r,ms);
        refresh_alert_events();
        if(indoor_utc) {
            if(history.add(indoor_air,indoor_utc))history_changed=true;
        } else {
            // An offline boot has no trusted civil UTC; retain RAM history
            // separately indexed by elapsed local hours until NTP appears.
            if(history.add_offline(indoor_air,ms))history_changed=true;
        }
        samples_ok++;
    } else if(result==RuuviAccept::Duplicate ||
              result==RuuviAccept::OldSequence) samples_duplicate++;
    else samples_rejected++;
}
class OnAdvertisement : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice device) override {
        if(!device.haveManufacturerData())return;
        const std::string raw=device.getManufacturerData();
        RuuviV5 parsed{};
        if(!decode_ruuvi_v5(
              reinterpret_cast<const uint8_t*>(raw.data()),
              raw.size(),device.getRSSI(),parsed))return;
        if(!guard)return;
        lock();
        discovered=parsed;have_discovered=true;
        const uint64_t ms=uptime_ms();
        if(indoor_binding.bound() &&
           parsed.mac==indoor_binding.mac()) {
            accept_indoor(parsed,ms);
        } else if(outdoor_binding.bound() &&
                  parsed.mac==outdoor_binding.mac()) {
            if(outdoor_binding.accept(parsed,ms)==RuuviAccept::Accepted) {
                const auto a=calculate_air_moisture(parsed.temperature_c,
                                                   parsed.relative_humidity_pct);
                if(a.valid){
                    outdoor_air=a;last_outdoor=parsed;
                    has_outdoor=true;outdoor_utc=utc_valid_now();
                    outdoor_mono_ms=ms;
                }
            }
        }
        unlock();
    }
};
bool read_mac_setting(Preferences& prefs,const char* key,
                      const char* fallback,RuuviBinding& dst) {
    const String stored=prefs.getString(key,"");
    const char* candidate=stored.length()?stored.c_str():fallback;
    std::array<uint8_t,6> mac{};
    if(parse_ruuvi_mac(candidate,mac)) {dst.bind(mac);return true;}
    return false;
}
String mac_string(const std::array<uint8_t,6>& mac) {
    char b[20];
    snprintf(b,sizeof(b),"%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return String(b);
}
bool save_config() {
    CottageModeConfig cfg{};
    String indoor_id,outdoor_id;
    lock();
    cfg=config;
    const AdvisoryLimits limits=warning_limits;
    indoor_id=indoor_binding.bound()?mac_string(indoor_binding.mac()):String("none");
    outdoor_id=outdoor_binding.bound()?mac_string(outdoor_binding.mac()):String("none");
    unlock();
    Preferences p;
    if(!p.begin("cottage_air",false))return false;
    bool saved=true;
    saved &= p.putUChar("mode",static_cast<uint8_t>(cfg.mode))>0;
    saved &= p.putFloat("target",static_cast<float>(cfg.hold_target_c))>0;
    saved &= p.putFloat("dryadd",static_cast<float>(cfg.drying_delta_c))>0;
    saved &= p.putFloat("rh70",static_cast<float>(limits.monitor_rh))>0;
    saved &= p.putFloat("rh80",static_cast<float>(limits.review_rh))>0;
    saved &= p.putFloat("lowvolt",static_cast<float>(limits.low_battery_v))>0;
    saved &= p.putString("indoor",indoor_id)>0;
    saved &= p.putString("outdoor",outdoor_id)>0;
    p.end();
    return saved;
}
void checkpoint() {
    const uint64_t ms=uptime_ms();
    if(ms-last_store_ms<checkpoint_ms)return;
    last_store_ms=ms;
    AirHistoryDump dump{};
    bool should_save=false;
    lock();
    // Never persist untrusted boot-relative UTC as if it were real history.
    if(history_changed && history.count() &&
       history.newest(0).hour_utc<0x80000000u) {
        dump=history.dump();history_changed=false;should_save=true;
    }
    unlock();
    if(!should_save)return;
    Preferences prefs;
    if(prefs.begin("cottage_hist",false)) {
        if(prefs.putBytes("hourly_v1",&dump,sizeof(dump))==sizeof(dump))
            stores++;
        else {
            lock();history_changed=true;unlock();
        }
        prefs.end();
    } else {
        lock();history_changed=true;unlock();
    }
}
bool parse_float(const String& input,double& result) {
    if(input.length()<1||input.length()>20)return false;
    const std::string s(input.c_str());
    char* end=nullptr;
    result=strtod(s.c_str(),&end);
    return end!=s.c_str()&&*end=='\0'&&std::isfinite(result);
}
bool check_token(WebServer& web) {
    return web.hasArg("token") && web.arg("token")==csrf_token;
}
void json_number_or_null(cJSON* o,const char* key,bool ok,double v) {
    if(ok&&std::isfinite(v))cJSON_AddNumberToObject(o,key,v);
    else cJSON_AddNullToObject(o,key);
}
} // namespace

void c23_initialize() {
    guard=xSemaphoreCreateMutex();
    if(!guard)return;
    snprintf(csrf_token,sizeof(csrf_token),"%08lX%08lX",
             static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()));
    Preferences prefs;
    bool config_loaded=false;
    if(prefs.begin("cottage_air",true)) {
        config_loaded=true;
        const uint8_t m=prefs.getUChar("mode",0);
        if(m<=2)config.mode=static_cast<CottageMode>(m);
        const double target=prefs.getFloat("target",12.0f);
        const double dryadd=prefs.getFloat("dryadd",4.0f);
        if(std::isfinite(target)&&target>=5&&target<=24)
            config.hold_target_c=target;
        if(std::isfinite(dryadd)&&dryadd>=3&&dryadd<=5)
            config.drying_delta_c=dryadd;
        const double rh70=prefs.getFloat("rh70",70.0f);
        const double rh80=prefs.getFloat("rh80",80.0f);
        const double low_v=prefs.getFloat("lowvolt",2.50f);
        if(std::isfinite(rh70)&&std::isfinite(rh80) &&
           rh70>=55&&rh70<=80 && rh80>=75&&rh80<=95 &&
           rh80>=rh70+5) {
            warning_limits.monitor_rh=rh70;
            warning_limits.review_rh=rh80;
        }
        if(std::isfinite(low_v)&&low_v>=2.0&&low_v<=3.2)
            warning_limits.low_battery_v=low_v;
        alarm_engine.update_limits(warning_limits);
        read_mac_setting(prefs,"indoor",COTTAGE_RUUVI_MAC,indoor_binding);
        #ifdef COTTAGE_OUTDOOR_RUUVI_MAC
        read_mac_setting(prefs,"outdoor",COTTAGE_OUTDOOR_RUUVI_MAC,outdoor_binding);
        #else
        read_mac_setting(prefs,"outdoor","",outdoor_binding);
        #endif
        prefs.end();
    }
    if(!config_loaded) {
        std::array<uint8_t,6> mac{};
        if(parse_ruuvi_mac(COTTAGE_RUUVI_MAC,mac))
            indoor_binding.bind(mac);
        #ifdef COTTAGE_OUTDOOR_RUUVI_MAC
        if(parse_ruuvi_mac(COTTAGE_OUTDOOR_RUUVI_MAC,mac))
            outdoor_binding.bind(mac);
        #endif
    }
    Preferences hp;
    if(hp.begin("cottage_hist",true)) {
        if(hp.getBytesLength("hourly_v1")==sizeof(AirHistoryDump)) {
            AirHistoryDump stored{};
            hp.getBytes("hourly_v1",&stored,sizeof(stored));
            history.restore(stored); // checksum and monotonic time validation
        }
        hp.end();
    }
    last_store_ms=uptime_ms();
}
void c23_ble_worker(void*) {
    // Keep existing, bundled Arduino/ESP-IDF BLE stack. NimBLE migration
    // requires a separate verified ESP-IDF/Arduino compatibility test.
    BLEDevice::init("EnevinaLiteC23");
    BLEScan* scan=BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new OnAdvertisement(),true);
    scan->setActiveScan(false);
    ble_initialized.store(true);
    for(;;) {
        {
            // The network-memory lock is needed only during BLE scanning.
            // Release it BEFORE the 12-second sleep so TLS and OTA can run.
            enevina_firmware::NetworkBudgetLease lease(0,96*1024,24*1024);
            if(lease.held()) {
                scan->start(4,false);
                enevina_firmware::network_budget_sample();
                scan->clearResults();
            } else {
                low_heap_skips++;
            }
        } // lease destructor releases network budget before waiting
        vTaskDelay(pdMS_TO_TICKS(12000));
    }
}
bool c23_ble_initialized() {return ble_initialized.load();}
void c23_tick() {
    if(!guard)return;
    const uint64_t ms=uptime_ms();
    if(ms-last_tick_ms<10000)return;
    last_tick_ms=ms;
    lock();
    alarm_engine.tick(ms,indoor_binding.bound());
    refresh_alert_events();
    unlock();
    checkpoint();
}
TemperatureSample c23_temperature_sample() {
    TemperatureSample out{};
    if(!guard)return out;
    lock();
    const uint64_t ms=uptime_ms();
    if(has_indoor && indoor_binding.valid(ms) && indoor_utc) {
        out.received=true;out.captured_utc=indoor_utc;
        out.indoor_c=indoor_air.temperature_c;
    }
    unlock();
    return out;
}
void c23_append_status(cJSON* root) {
    // Use an actual outdoor RuuviTag when bound; otherwise FMI is
    // explicitly only a forecast for a thermal offset, NEVER an RH source.
    const auto weather=enevina_firmware::weather_status();
    const int64_t now_utc=utc_valid_now();
    const bool forecast_ok=now_utc && weather.available &&
      weather.fetched_utc>0 && now_utc>=weather.fetched_utc &&
      now_utc-weather.fetched_utc<=6*3600 &&
      weather.next_utc>=now_utc && weather.next_utc<=now_utc+3600 &&
      std::isfinite(weather.next_temperature_c);
    cJSON* air=cJSON_CreateObject();
    if(!air)return;
    if(!guard) {cJSON_AddItemToObject(root,"air",air);return;}
    lock();
    const uint64_t ms=uptime_ms();
    const bool valid=has_indoor && indoor_binding.valid(ms);
    const bool outside_valid=has_outdoor && outdoor_binding.valid(ms);
    const auto vent=estimate_ventilation(
        valid?indoor_air:AirMoisture{},
        outside_valid?outdoor_air:AirMoisture{});
    const bool have_out_t=outside_valid || forecast_ok;
    const double exterior_c=outside_valid?outdoor_air.temperature_c:
                            weather.next_temperature_c;
    const auto plan=evaluate_cottage_mode(config,indoor_air,valid,
        have_out_t,exterior_c,alarm_engine.high70_duration_ms(ms));
    cJSON_AddNumberToObject(air,"mode",static_cast<int>(config.mode));
    cJSON_AddNumberToObject(air,"hold_target_c",config.hold_target_c);
    cJSON_AddNumberToObject(air,"drying_delta_c",config.drying_delta_c);
    cJSON_AddNumberToObject(air,"warning_rh70",warning_limits.monitor_rh);
    cJSON_AddNumberToObject(air,"warning_rh80",warning_limits.review_rh);
    cJSON_AddNumberToObject(air,"warning_battery_v",warning_limits.low_battery_v);
    cJSON_AddBoolToObject(air,"bound",indoor_binding.bound());
    cJSON_AddBoolToObject(air,"sensor_fresh",valid);
    cJSON_AddNumberToObject(air,"sample_count",samples_ok);
    cJSON_AddNumberToObject(air,"duplicate_count",samples_duplicate);
    cJSON_AddNumberToObject(air,"rejected_count",samples_rejected);
    cJSON_AddNumberToObject(air,"history_hours",history.count());
    bool trend_ready=false;
    double rh_trend=0,abs_trend=0;
    if(history.count()>=7) {
        const auto latest=history.newest(0);
        const auto prior=history.newest(6);
        if(latest.hour_utc>=prior.hour_utc+6 &&
           latest.hour_utc<=prior.hour_utc+10) {
            trend_ready=true;
            rh_trend=(static_cast<int>(latest.rh_centi)-
                      static_cast<int>(prior.rh_centi))/100.0;
            abs_trend=(static_cast<int>(latest.absolute_humidity_centi)-
                       static_cast<int>(prior.absolute_humidity_centi))/100.0;
        }
    }
    json_number_or_null(air,"rh_6h_change_pct",trend_ready,rh_trend);
    json_number_or_null(air,"absolute_6h_change_gm3",trend_ready,abs_trend);
    cJSON_AddNumberToObject(air,"nvs_checkpoints",stores);
    cJSON_AddNumberToObject(air,"low_heap_scan_skips",low_heap_skips);
    cJSON_AddBoolToObject(air,"sensor_discovered",have_discovered);
    // Local web only; the saved MAC and full sensor ID are not printed
    // to UART logs, external APIs or GitHub.
    if(have_discovered)cJSON_AddStringToObject(air,"discovered_mac",
                                                 mac_string(discovered.mac).c_str());
    if(indoor_binding.bound())cJSON_AddStringToObject(air,"bound_mac",
                                                 mac_string(indoor_binding.mac()).c_str());
    cJSON_AddStringToObject(air,"config_token",csrf_token);
    json_number_or_null(air,"temperature_c",valid,indoor_air.temperature_c);
    json_number_or_null(air,"rh_pct",valid,indoor_air.rh_pct);
    json_number_or_null(air,"pressure_pa",valid,last_indoor.pressure_pa);
    json_number_or_null(air,"battery_v",valid,last_indoor.battery_v);
    json_number_or_null(air,"rssi_dbm",valid,last_indoor.rssi_dbm);
    json_number_or_null(air,"dewpoint_c",valid,indoor_air.dew_point_c);
    json_number_or_null(air,"absolute_gm3",valid,indoor_air.absolute_humidity_gm3);
    json_number_or_null(air,"vapour_pressure_pa",valid,indoor_air.vapour_pressure_pa);
    json_number_or_null(air,"age_s",has_indoor&&ms>=indoor_mono_ms,
                        has_indoor?(ms-indoor_mono_ms)/1000.0:0);
    cJSON_AddNumberToObject(air,"last_utc",static_cast<double>(indoor_utc));
    cJSON_AddBoolToObject(air,"outside_sensor_fresh",outside_valid);
    cJSON_AddStringToObject(air,"outside_temp_source",
        outside_valid?"RUUVI_RAWV2":forecast_ok?"FMI_FORECAST":"NONE");
    json_number_or_null(air,"outside_temperature_c",have_out_t,exterior_c);
    json_number_or_null(air,"outside_rh_pct",outside_valid,
                        outdoor_air.rh_pct);
    json_number_or_null(air,"outside_absolute_gm3",outside_valid,
                        outdoor_air.absolute_humidity_gm3);
    json_number_or_null(air,"inside_outside_c",vent.available,
                        vent.inside_outside_c);
    json_number_or_null(air,"drying_potential_gm3",vent.available,
                        vent.inside_minus_outside_gm3);
    cJSON_AddBoolToObject(air,"possible_ventilation_drying",
                          vent.available && vent.possible_drying);
    cJSON_AddBoolToObject(air,"target_available",plan.has_target);
    json_number_or_null(air,"target_c",plan.has_target,plan.target_c);
    cJSON_AddBoolToObject(air,"target_below_16",
                          plan.direct_setpoint_below_mitsubishi_min);
    cJSON_AddBoolToObject(air,"advisory_heat_needed",plan.advisory_heat_needed);
    cJSON_AddNumberToObject(air,"advisory",static_cast<int>(plan.advisory));
    cJSON_AddBoolToObject(air,"physical_writes_allowed",false);
    json_number_or_null(air,"rh70_duration_h",valid,
                        alarm_engine.high70_duration_ms(ms)/3600000.0);
    json_number_or_null(air,"rh80_duration_h",valid,
                        alarm_engine.high80_duration_ms(ms)/3600000.0);
    cJSON* arr=cJSON_CreateArray();
    for(size_t i=0;i<moisture_alert_count;i++) {
        auto a=alarm_engine.alert(static_cast<MoistureAlertKind>(i));
        cJSON* o=cJSON_CreateObject();
        cJSON_AddNumberToObject(o,"kind",i);
        cJSON_AddBoolToObject(o,"active",a.active);
        cJSON_AddBoolToObject(o,"ack",a.acknowledged);
        cJSON_AddItemToArray(arr,o);
    }
    cJSON_AddItemToObject(air,"alerts",arr);
    cJSON* log=cJSON_CreateArray();
    for(size_t i=0;i<events_count && i<20;i++) {
        auto& e=events[(events_next+events.size()-1-i)%events.size()];
        cJSON* o=cJSON_CreateObject();
        cJSON_AddNumberToObject(o,"ts_utc",static_cast<double>(e.utc));
        cJSON_AddNumberToObject(o,"uptime_ms",static_cast<double>(e.monotonic_ms));
        cJSON_AddNumberToObject(o,"kind",e.kind);
        cJSON_AddNumberToObject(o,"code",e.code);
        cJSON_AddItemToArray(log,o);
    }
    cJSON_AddItemToObject(air,"events",log);
    unlock();
    cJSON_AddItemToObject(root,"air",air);
}
void c23_history_http(WebServer& web) {
    if(!guard) {web.send(503,"application/json","{}");return;}
    // 168 records * compact JSON, streamed instead of large allocations.
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json","");web.sendContent("{\"hours\":[");
    size_t n=0;
    lock();n=history.count();unlock();
    for(size_t i=0;i<n;i++) {
        AirHistoryPoint p{};
        lock();p=history.newest(n-1-i);unlock();
        char line[180];
        snprintf(line,sizeof(line),
           "%s{\"hour\":%lu,\"clock_valid\":%s,\"temp\":%.2f,\"rh\":%.2f,\"dew\":%.2f,\"abs\":%.2f}",
           i?",":"",static_cast<unsigned long>(p.hour_utc),
           (p.hour_utc<0x80000000u)?"true":"false",
           p.temperature_centi/100.0,p.rh_centi/100.0,
           p.dewpoint_centi/100.0,p.absolute_humidity_centi/100.0);
        web.sendContent(line);
        if(i%16==0)delay(1);
    }
    web.sendContent("]}");web.sendContent("");
}
void c23_register_routes(WebServer& web) {
    // Only local settings are mutable; NO CN105, Shelly or physical writes.
    web.on("/api/air/history",HTTP_GET,[&web](){c23_history_http(web);});
    web.on("/api/air/config",HTTP_POST,[&web](){
        if(!check_token(web)) {web.send(403,"text/plain","Token invalid");return;}
        // Atomically validate all supplied fields before changing saved state.
        if(!web.hasArg("mode")||!web.hasArg("hold")||!web.hasArg("delta")) {
            web.send(400,"text/plain","Missing mode values");return;
        }
        const String mode_s=web.arg("mode");
        double hold=0,delta=0;
        if(mode_s!="0"&&mode_s!="1"&&mode_s!="2") {
            web.send(400,"text/plain","Invalid mode");return;
        }
        if(!parse_float(web.arg("hold"),hold)||hold<5||hold>24 ||
           !parse_float(web.arg("delta"),delta)||delta<3||delta>5) {
            web.send(400,"text/plain","Invalid target");return;
        }
        lock();
        config.mode=static_cast<CottageMode>(mode_s.toInt());
        config.hold_target_c=hold;
        config.drying_delta_c=delta;
        log_event(3,static_cast<uint8_t>(config.mode));
        unlock();
        const bool saved=save_config();
        web.send(saved?200:500,"application/json",
                 saved?"{\"saved\":true,\"physical_writes\":false}":
                       "{\"saved\":false,\"physical_writes\":false}");
    });
    web.on("/api/air/thresholds",HTTP_POST,[&web](){
        if(!check_token(web)) {web.send(403,"text/plain","Invalid token");return;}
        double rh70=0,rh80=0,low_batt=0;
        if(!parse_float(web.arg("rh70"),rh70) ||
           !parse_float(web.arg("rh80"),rh80) ||
           !parse_float(web.arg("battery"),low_batt) ||
           rh70<55||rh70>80||rh80<75||rh80>95 ||
           rh80<rh70+5||low_batt<2||low_batt>3.2) {
            web.send(400,"text/plain","Bad thresholds");return;
        }
        lock();
        warning_limits.monitor_rh=rh70;
        warning_limits.review_rh=rh80;
        warning_limits.low_battery_v=low_batt;
        alarm_engine.update_limits(warning_limits);
        log_event(6,0);
        unlock();
        const bool saved=save_config();
        web.send(saved?200:500,"application/json",
                 saved?"{\"saved\":true,\"physical_writes\":false}":
                       "{\"saved\":false,\"physical_writes\":false}");
    });
    web.on("/api/air/bind",HTTP_POST,[&web](){
        if(!check_token(web)) {web.send(403,"text/plain","Token invalid");return;}
        const String kind=web.arg("which"),value=web.arg("mac");
        if(kind!="indoor"&&kind!="outdoor") {
            web.send(400,"text/plain","Unknown sensor role");return;
        }
        std::array<uint8_t,6> mac{};
        const bool unbind=value=="none";
        if(!unbind && !parse_ruuvi_mac(value.c_str(),mac)) {
            web.send(400,"text/plain","Bad MAC");return;
        }
        bool cleared_indoor_history=false;
        lock();
        if(kind=="indoor") {
            const bool binding_changed=unbind?indoor_binding.bound():
                (!indoor_binding.bound()||indoor_binding.mac()!=mac);
            if(binding_changed) {
                if(unbind) indoor_binding.unbind();
                else indoor_binding.bind(mac);
                has_indoor=false;indoor_utc=0;indoor_air={};
                alarm_engine=MoistureMonitor(warning_limits);
                history=AirHistory{}; // never mix two sensors in one series
                history_changed=false;
                last_store_ms=uptime_ms();
                cleared_indoor_history=true;
            }
        } else {
            if(unbind)outdoor_binding.unbind();
            else outdoor_binding.bind(mac);
            has_outdoor=false;outdoor_utc=0;outdoor_air={};
        }
        log_event(4,kind=="indoor"?0:1);
        unlock();
        if(cleared_indoor_history) {
            // New binding must not be persisted if old sensor history cannot
            // be cleared from NVS first (prevents mixing after reboot).
            Preferences p;
            bool purged=false;
            if(p.begin("cottage_hist",false)) {
                purged=p.getBytesLength("hourly_v1")==0 ||
                       p.remove("hourly_v1");
                p.end();
            }
            if(!purged) {
                web.send(500,"text/plain","History NVS purge failed");
                return;
            }
        }
        const bool saved=save_config();
        web.send(saved?200:500,"application/json",
                 saved?"{\"saved\":true,\"physical_writes\":false}":
                       "{\"saved\":false,\"physical_writes\":false}");
    });
    web.on("/api/air/ack",HTTP_POST,[&web](){
        if(!check_token(web)||!web.hasArg("alert")) {
            web.send(403,"text/plain","Missing token");return;
        }
        const String val=web.arg("alert");
        if(val.length()!=1||val[0]<'0'||val[0]>'4') {
            web.send(400,"text/plain","Unknown alert");return;
        }
        lock();alarm_engine.acknowledge(
            static_cast<MoistureAlertKind>(val[0]-'0'));
        log_event(5,static_cast<uint8_t>(val[0]-'0'));unlock();
        web.send(200,"application/json","{\"acknowledged\":true}");
    });
}
} // namespace enevina::cottage
