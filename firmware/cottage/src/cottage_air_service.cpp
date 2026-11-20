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
#include "cottage_site_settings.hpp"
#include "cottage_wifi.hpp"
#include "cottage_network_budget.hpp"
#include "cottage/memory_policy.hpp"
#include <atomic>
#include "cottage/ruuvi_v5.hpp"
#include "cottage/ruuvi_registry.hpp"
#include "cottage/air_humidity.hpp"
#include "../../main/weather.hpp"

namespace enevina::cottage {
namespace {
SemaphoreHandle_t guard=nullptr;
RuuviBinding indoor_binding,outdoor_binding;
AirHistory history;
MoistureMonitor alarm_engine;
CottageModeConfig config{};
uint32_t config_generation=0;
bool config_storage_verified=true;  // only false after ambiguous NVS partial failure
AdvisoryLimits warning_limits{};
AirMoisture indoor_air{},outdoor_air{};
RuuviV5 last_indoor{},last_outdoor{},discovered{};
// All nearby RuuviTags (for the setup list) and the owner's names for them.
// Guarded by `guard`, like the bindings.
RuuviRegistry sensor_registry;
SensorNames sensor_names;
int64_t indoor_utc=0,outdoor_utc=0;
uint64_t indoor_mono_ms=0,outdoor_mono_ms=0;
uint64_t last_store_ms=0,last_tick_ms=0;
bool has_indoor=false,has_outdoor=false,have_discovered=false;
bool history_changed=false;
// Fail closed: a newly bound sensor's hourly history is not persisted
// until its MAC has been successfully saved to configuration NVS.
bool indoor_binding_persisted=true;
// Protected by guard. A failed boot-time purge must block ALL later config
// writes until the unowned legacy history is really removed.
bool history_origin_clean=true;
uint32_t samples_ok=0,samples_rejected=0,samples_duplicate=0,stores=0;
uint32_t low_heap_skips=0;
// Cross-task status counters are atomic; never hold the Ruuvi callback lock
// while sampling heap or when acquiring the shared network memory mutex.
std::atomic<uint32_t> ble_scan_attempts{0};
std::atomic<uint32_t> ble_scan_completed{0};
std::atomic<uint32_t> ble_scan_memory_denials{0};
std::atomic<uint32_t> ble_scan_busy_denials{0};
std::atomic<uint32_t> ble_scan_last_free_internal{0};
std::atomic<uint32_t> ble_scan_last_largest_internal{0};
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
        sensor_registry.observe(parsed,ms);
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
    if(!history_origin_clean) {
        // A failed boot purge must not be bypassed by /air/config,
        // /air/thresholds or a same-MAC /air/bind. Retry the purge
        // under the same mutex used by the hourly checkpoint.
        Preferences hp;
        bool purged=false;
        if(hp.begin("cottage_hist",false)) {
            purged=!hp.isKey("hourly_v1")||hp.remove("hourly_v1");
            hp.end();
        }
        if(!purged) {
            indoor_binding_persisted=false;
            unlock();
            return false;
        }
        history=AirHistory{};
        history_changed=false;
        history_origin_clean=true;
    }
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
    saved &= p.putFloat("occupied",static_cast<float>(cfg.occupied_target_c))>0;
    saved &= p.putFloat("dryadd",static_cast<float>(cfg.drying_delta_c))>0;
    saved &= p.putFloat("rh70",static_cast<float>(limits.monitor_rh))>0;
    saved &= p.putFloat("rh80",static_cast<float>(limits.review_rh))>0;
    saved &= p.putFloat("lowvolt",static_cast<float>(limits.low_battery_v))>0;
    saved &= p.putString("indoor",indoor_id)>0;
    saved &= p.putString("outdoor",outdoor_id)>0;
    p.end();
    if(saved) {
        lock();
        indoor_binding_persisted=true;
        unlock();
    }
    return saved;
}
// Failure-atomic as observed by the owner: don't report a rolled-back target
// until NVS itself proves that the old target, mode and delta are durable.
// Preferences::put* writes independent keys, so a later failed key can leave
// earlier changes in flash even when save_config() returned false.
bool persisted_mode_matches(const CottageModeConfig& cfg) {
    Preferences p;
    if(!p.begin("cottage_air",true))return false;
    const bool present=p.isKey("mode")&&p.isKey("target")&&
                       p.isKey("occupied")&&p.isKey("dryadd");
    const bool match=present &&
        p.getUChar("mode",255)==static_cast<uint8_t>(cfg.mode) &&
        std::fabs(p.getFloat("target",NAN)-cfg.hold_target_c)<0.001 &&
        std::fabs(p.getFloat("occupied",NAN)-cfg.occupied_target_c)<0.001 &&
        std::fabs(p.getFloat("dryadd",NAN)-cfg.drying_delta_c)<0.001;
    p.end();
    return match;
}

void checkpoint() {
    const uint64_t ms=uptime_ms();
    if(ms-last_store_ms<checkpoint_ms)return;
    last_store_ms=ms;
    lock();
    // Bind and checkpoint must serialize the full NVS write with the
    // same guard. Taking a snapshot and unlocking before putBytes()
    // allows an old sensor's checkpoint to race behind a MAC change.
    if(!history_origin_clean || !indoor_binding_persisted ||
       !history_changed ||
       !history.count() ||
       history.newest(0).hour_utc>=0x80000000u) {
        unlock();return;
    }
    const AirHistoryDump dump=history.dump();
    Preferences prefs;
    if(prefs.begin("cottage_hist",false)) {
        if(prefs.putBytes("hourly_v1",&dump,sizeof(dump))==sizeof(dump)) {
            history_changed=false;
            stores++;
        }
        prefs.end();
    }
    // NVS failure keeps history_changed set for the next checkpoint.
    unlock();
}
bool parse_float(const String& input,double& result) {
    if(input.length()<1||input.length()>20)return false;
    const std::string s(input.c_str());
    char* end=nullptr;
    result=strtod(s.c_str(),&end);
    return end!=s.c_str()&&*end=='\0'&&std::isfinite(result);
}
constexpr char kNamesNs[]="enevina_names";
constexpr char kNamesKey[]="v1";
// Names are labels only. A failed save leaves RAM and NVS consistent: the
// caller restores the previous table.
bool save_sensor_names(const SensorNames& names) {
    Preferences p;
    if(!p.begin(kNamesNs,false)) return false;
    const auto& table=names.entries();
    const size_t written=p.putBytes(kNamesKey,table.data(),sizeof(table));
    p.end();
    return written==sizeof(table);
}
void load_sensor_names() {
    Preferences p;
    if(!p.begin(kNamesNs,true)) return;
    std::array<SensorNameEntry,kSensorNameSlots> raw{};
    if(p.getBytesLength(kNamesKey)==sizeof(raw) &&
       p.getBytes(kNamesKey,raw.data(),sizeof(raw))==sizeof(raw)) {
        SensorNames loaded;
        if(loaded.load(raw)) sensor_names=loaded;   // corrupt blob -> no names, never junk
    }
    p.end();
}
void send_sensors(WebServer& web) {
    RuuviSeen seen[kRuuviRegistrySlots];
    std::array<uint8_t,6> indoor{},outdoor{};
    bool has_in=false,has_out=false;
    size_t count=0;
    String names[kRuuviRegistrySlots];
    String bound_in,bound_out,bound_in_name,bound_out_name;
    const uint64_t now=uptime_ms();
    lock();
    count=sensor_registry.snapshot(now,seen,kRuuviRegistrySlots);
    has_in=indoor_binding.bound();has_out=outdoor_binding.bound();
    if(has_in) {indoor=indoor_binding.mac();bound_in=mac_string(indoor);}
    if(has_out) {outdoor=outdoor_binding.mac();bound_out=mac_string(outdoor);}
    for(size_t i=0;i<count;++i) {
        const char* n=sensor_names.get(seen[i].latest.mac);
        if(n) names[i]=n;
    }
    if(has_in) {const char* n=sensor_names.get(indoor);if(n) bound_in_name=n;}
    if(has_out) {const char* n=sensor_names.get(outdoor);if(n) bound_out_name=n;}
    unlock();
    cJSON* root=cJSON_CreateObject();
    cJSON* list=root?cJSON_AddArrayToObject(root,"sensors"):nullptr;
    if(!list) {cJSON_Delete(root);web.send(503,"application/json","{}");return;}
    cJSON_AddNumberToObject(root,"scan_attempts",ble_scan_attempts.load());
    cJSON_AddNumberToObject(root,"scan_completed",ble_scan_completed.load());
    cJSON_AddNumberToObject(root,"scan_memory_denials",ble_scan_memory_denials.load());
    cJSON_AddNumberToObject(root,"scan_busy_denials",ble_scan_busy_denials.load());
    cJSON_AddBoolToObject(root,"ble_ready",c23_ble_initialized());
    cJSON* bound=cJSON_AddObjectToObject(root,"bound");
    if(bound) {
        if(has_in) {cJSON_AddStringToObject(bound,"indoor",bound_in.c_str());
            cJSON_AddStringToObject(bound,"indoor_name",bound_in_name.c_str());}
        else cJSON_AddNullToObject(bound,"indoor");
        if(has_out) {cJSON_AddStringToObject(bound,"outdoor",bound_out.c_str());
            cJSON_AddStringToObject(bound,"outdoor_name",bound_out_name.c_str());}
        else cJSON_AddNullToObject(bound,"outdoor");
    }
    for(size_t i=0;i<count;++i) {
        cJSON* o=cJSON_CreateObject();
        if(!o) break;
        const auto& e=seen[i];
        // Freshness follows the last ACCEPTED measurement, not the last frame
        // heard: a sensor replaying one sequence is audible but not measuring.
        const uint64_t heard_ms=now>=e.last_ms?now-e.last_ms:0;
        const uint64_t age_ms=e.has_measurement&&now>=e.measured_ms?now-e.measured_ms:heard_ms;
        cJSON_AddStringToObject(o,"mac",mac_string(e.latest.mac).c_str());
        cJSON_AddStringToObject(o,"name",names[i].c_str());
        const char* role=(has_in&&e.latest.mac==indoor)?"indoor":(has_out&&e.latest.mac==outdoor)?"outdoor":"";
        cJSON_AddStringToObject(o,"role",role);
        const RuuviV5& m=e.has_measurement?e.measured:e.latest;
        cJSON_AddNumberToObject(o,"temperature_c",m.temperature_c);
        cJSON_AddNumberToObject(o,"humidity_pct",m.relative_humidity_pct);
        cJSON_AddNumberToObject(o,"battery_v",m.battery_v);
        cJSON_AddNumberToObject(o,"rssi_dbm",e.latest.rssi_dbm);
        cJSON_AddNumberToObject(o,"age_s",static_cast<double>(age_ms/1000));
        cJSON_AddNumberToObject(o,"heard_age_s",static_cast<double>(heard_ms/1000));
        cJSON_AddBoolToObject(o,"live",e.has_measurement&&age_ms<=kRuuviFreshMs);
        cJSON_AddNumberToObject(o,"frames",e.frames);
        cJSON_AddItemToArray(list,o);
    }
    char* body=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!body) {web.send(503,"application/json","{}");return;}
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8",body);
    cJSON_free(body);
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
    load_sensor_names();
    if(!guard)return;
    snprintf(csrf_token,sizeof(csrf_token),"%08lX%08lX",
             static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()));
    Preferences prefs;
    bool config_loaded=false;
    std::array<uint8_t,6> stored_indoor_mac{};
    bool stored_indoor_mac_valid=false;
    if(prefs.begin("cottage_air",true)) {
        config_loaded=true;
        const uint8_t m=prefs.getUChar("mode",0);
        if(m<=3)config.mode=static_cast<CottageMode>(m);
        const double target=prefs.getFloat("target",12.0f);
        const double dryadd=prefs.getFloat("dryadd",4.0f);
        if(std::isfinite(target)&&target>=5&&target<=24)
            config.hold_target_c=target;
        const double occupied=prefs.getFloat("occupied",21.0f);
        if(std::isfinite(occupied)&&occupied>=16&&occupied<=26)
            config.occupied_target_c=occupied;
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
        // Do not mistake a compile-time fallback for an NVS-proven owner
        // of an old hourly_v1 series. Persisted identity is separate.
        const String persisted_id=prefs.getString("indoor","");
        stored_indoor_mac_valid=parse_ruuvi_mac(
            persisted_id.c_str(),stored_indoor_mac);
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
    const bool origin_verified=verified_history_origin(
        stored_indoor_mac_valid,stored_indoor_mac,indoor_binding);
    // Existing hourly_v1 has no embedded MAC. An unseen/changed compiler
    // fallback is not allowed to relabel another RuuviTag's history.
    indoor_binding_persisted=origin_verified || !indoor_binding.bound();
    bool old_history_cleared=false;
    Preferences hp;
    if(hp.begin("cottage_hist",origin_verified)) {
        if(origin_verified) {
            if(hp.getBytesLength("hourly_v1")==sizeof(AirHistoryDump)) {
                AirHistoryDump stored{};
                if(hp.getBytes("hourly_v1",&stored,sizeof(stored))==
                   sizeof(stored))
                    history.restore(stored); // CRC and UTC consistency
            }
        } else {
            // Purge unknown-origin history BEFORE persisting a new
            // compile-time fallback binding. A crash between these steps
            // cannot make A's old history appear to belong to B.
            old_history_cleared=hp.getBytesLength("hourly_v1")==0 ||
                                hp.remove("hourly_v1");
        }
        hp.end();
    }
    history_origin_clean=origin_verified || old_history_cleared;
    if(!origin_verified && indoor_binding.bound()) {
        // Preserve persistent history for compiler-only provisioning after
        // the old untagged series is safely cleared. No purge or NVS config
        // failure can authorize a future checkpoint for the new binding.
        if(!old_history_cleared || !save_config()) {
            indoor_binding_persisted=false;
            Serial.println("C23_HISTORY_SOURCE_UNVERIFIED: checkpoint blocked");
        }
    }
    // PREPARED is never proof that the site settings committed: restore the
    // old band even if the old and new fields happen to be identical.
    // SITE_COMMITTED is a read-back verified transition; only that phase may
    // replay the air target. All reconciliation must precede controller tasks.
    if(site_comfort_transaction_pending()) {
        ComfortTxn tx{};
        const bool valid=site_comfort_transaction_read(tx);
        const bool durable=persisted_mode_matches(config);
        const auto action=valid?comfort_recovery_action(
            tx,site_settings_snapshot(),config,durable):
            ComfortRecoveryAction::Block;
        if(action==ComfortRecoveryAction::Block) {
            config_storage_verified=false;
            Serial.println("C23_COMFORT_JOURNAL_UNVERIFIED: commands blocked");
        } else if(action==ComfortRecoveryAction::RestorePriorSite) {
            // A changed mode / a partial site commit does not strand the
            // device. Recommit the former site limits, preserving the user's
            // latest verified mode if compatible with those former limits.
            if(!site_comfort_transaction_abort_verified(config))
                config_storage_verified=false;
        } else {
            CottageModeConfig recovered=config;
            comfort_apply_target(tx,recovered);
            if(!persisted_mode_matches(recovered)) {
                lock();config=recovered;config_storage_verified=false;unlock();
                save_config();
            }
            if(persisted_mode_matches(recovered)) {
                lock();config=recovered;config_storage_verified=true;unlock();
                if(!site_comfort_transaction_finish(recovered))
                    config_storage_verified=false;
            } else {
                config_storage_verified=false;
                Serial.println("C23_COMFORT_RECOVERY_FAILED: commands blocked");
            }
        }
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
        const auto internal_free=static_cast<uint32_t>(heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        const auto largest_free=static_cast<uint32_t>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        ble_scan_last_free_internal.store(internal_free);
        ble_scan_last_largest_internal.store(largest_free);
        const bool heap_ok=internal_memory_admissible(
            internal_free,largest_free,
            ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes);
        {
            // The network-memory lock is needed only during BLE scanning.
            // Release it BEFORE the 12-second sleep so TLS and OTA can run.
            enevina_firmware::NetworkBudgetLease lease(0,
                ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes);
            if(lease.held()) {
                ble_scan_attempts.fetch_add(1);
                scan->start(4,false);
                enevina_firmware::network_budget_sample();
                scan->clearResults();
                ble_scan_completed.fetch_add(1);
            } else {
                low_heap_skips++;
                if(heap_ok)ble_scan_busy_denials.fetch_add(1);
                else ble_scan_memory_denials.fetch_add(1);
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
void c23_history_air(double& rh_pct,bool& rh_ok,double& outdoor_c,bool& outdoor_ok) {
    rh_ok=outdoor_ok=false;
    if(!guard)return;
    lock();
    const uint64_t ms=uptime_ms();
    if(has_indoor && indoor_binding.valid(ms) && indoor_air.valid) {
        rh_pct=indoor_air.rh_pct;rh_ok=true;
    }
    if(has_outdoor && outdoor_binding.valid(ms) && outdoor_air.valid) {
        outdoor_c=outdoor_air.temperature_c;outdoor_ok=true;
    }
    unlock();
}
const char* c23_config_token() { return csrf_token; }
bool c23_authorize_local_setting(const char* submitted_token) {
    return guard && submitted_token && strlen(submitted_token)==strlen(csrf_token) &&
           strcmp(submitted_token,csrf_token)==0;
}
// Same mode target and outside-FMI/Ruuvi inputs as /api/status.
CottageModePlan c23_current_mode_plan() {
    if(!guard)return CottageModePlan{};
    const auto weather=enevina_firmware::weather_status();
    const int64_t now_utc=utc_valid_now();
    const bool forecast_ok=now_utc && weather.available &&
      weather.fetched_utc>0 && now_utc>=weather.fetched_utc &&
      now_utc-weather.fetched_utc<=6*3600 &&
      weather.next_utc>=now_utc && weather.next_utc<=now_utc+3600 &&
      std::isfinite(weather.next_temperature_c);
    lock();
    const uint64_t ms=uptime_ms();
    const bool fresh=has_indoor&&indoor_binding.valid(ms);
    const bool outside_fresh=has_outdoor&&outdoor_binding.valid(ms);
    const bool outside_ok=outside_fresh||forecast_ok;
    const double outside_c=outside_fresh?outdoor_air.temperature_c:
                            weather.next_temperature_c;
    const CottageModePlan plan=evaluate_cottage_mode(
        config,indoor_air,fresh,outside_ok,outside_c,
        alarm_engine.high70_duration_ms(ms));
    unlock();
    return plan;
}
uint32_t c23_config_generation() {
    if(!guard)return 0;
    lock();const uint32_t g=config_generation;unlock();return g;
}
bool c23_mode_config_is_durable(const CottageModeConfig& cfg) {
    return guard && persisted_mode_matches(cfg);
}
bool c23_config_storage_verified() {
    if(!guard)return false;
    lock();const bool ok=config_storage_verified;unlock();
    // A site save may have committed while the air save is still pending.
    // Pending intent is always a physical-write gate, including this boot.
    return ok && !site_comfort_transaction_pending();
}
CottageModeConfig c23_mode_config() {
    if(!guard) return CottageModeConfig{};
    lock();const auto snapshot=config;unlock();
    return snapshot;
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
    cJSON_AddNumberToObject(air,"occupied_target_c",config.occupied_target_c);
    cJSON_AddNumberToObject(air,"drying_delta_c",config.drying_delta_c);
    cJSON_AddBoolToObject(air,"config_storage_verified",
                          config_storage_verified&&!site_comfort_transaction_pending());
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
    cJSON_AddNumberToObject(air,"ble_scan_attempts",ble_scan_attempts.load());
    cJSON_AddNumberToObject(air,"ble_scan_completed",ble_scan_completed.load());
    cJSON_AddNumberToObject(air,"ble_scan_memory_denials",
                            ble_scan_memory_denials.load());
    cJSON_AddNumberToObject(air,"ble_scan_busy_denials",
                            ble_scan_busy_denials.load());
    cJSON_AddNumberToObject(air,"ble_scan_last_free_internal",
                            ble_scan_last_free_internal.load());
    cJSON_AddNumberToObject(air,"ble_scan_last_largest_internal",
                            ble_scan_last_largest_internal.load());
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
    web.on("/api/air/history",HTTP_GET,[&web](){
        if(cottage_wifi_require_home_lan(web))c23_history_http(web);
    });
    web.on("/api/air/config",HTTP_POST,[&web](){
        if(!cottage_wifi_require_home_lan(web))return;
        if(!check_token(web)) {web.send(403,"text/plain","Token invalid");return;}
        // Atomically validate all supplied fields before changing saved state.
        if(!web.hasArg("mode")||!web.hasArg("hold")||!web.hasArg("delta")) {
            web.send(400,"text/plain","Missing mode values");return;
        }
        const String mode_s=web.arg("mode");
        double hold=0,delta=0,occupied=0;
        if(mode_s!="0"&&mode_s!="1"&&mode_s!="2"&&mode_s!="3") {
            web.send(400,"text/plain","Invalid mode");return;
        }
        if(!parse_float(web.arg("hold"),hold)||hold<5||hold>24 ||
           !parse_float(web.arg("delta"),delta)||delta<3||delta>5) {
            web.send(400,"text/plain","Invalid target");return;
        }
        // Existing clients may omit the newly introduced occupied value:
        // preserve the currently stored value instead of resetting it.
        if(web.hasArg("occupied")) {
            if(!parse_float(web.arg("occupied"),occupied) ||
               occupied<16||occupied>26 ||
               std::fabs(occupied*2.0-std::round(occupied*2.0))>1e-7) {
                web.send(400,"text/plain","Invalid occupied target");return;
            }
        } else {
            lock();occupied=config.occupied_target_c;unlock();
        }
        CottageModeConfig proposed=c23_mode_config();
        proposed.mode=static_cast<CottageMode>(mode_s.toInt());
        proposed.hold_target_c=hold;
        proposed.occupied_target_c=occupied;
        proposed.drying_delta_c=delta;
        // With a pending Comfort transaction the second HTTP step may be
        // retried, or the owner may deliberately select another mode. A new
        // mode first cancels the old intent using only a VERIFIED old mode.
        if(site_comfort_transaction_pending()) {
            ComfortTxn tx{};
            if(!site_comfort_transaction_read(tx)) {
                web.send(503,"application/json",
                    "{\"saved\":false,\"error\":\"COMFORT_JOURNAL_UNVERIFIED\"}");
                return;
            }
            if(!comfort_mode_matches(tx,proposed)) {
                const auto previous=c23_mode_config();
                if(!persisted_mode_matches(previous) ||
                   !site_comfort_transaction_abort_verified(previous)) {
                    web.send(409,"application/json",
                        "{\"saved\":false,\"error\":\"COMFORT_RECOVERY_REQUIRED\"}");
                    return;
                }
            }
        }
        if(!mode_comfort_limits_fit(site_settings_snapshot(),proposed)) {
            web.send(409,"application/json",
                     "{\"saved\":false,\"error\":\"COMFORT_TARGET_CONFLICT\"}");
            return;
        }
        lock();
        const CottageModeConfig previous=config;
        config.mode=static_cast<CottageMode>(mode_s.toInt());
        config.hold_target_c=hold;
        config.occupied_target_c=occupied;
        config.drying_delta_c=delta;
        const CottageModeConfig desired=config;
        // Do not issue pump writes while the multi-key NVS transaction is
        // incomplete; RAM is NOT evidence of durable config.
        config_storage_verified=false;
        ++config_generation;
        unlock();
        save_config(); // durable status is proved by readback, not this bool
        // A late NVS failure can occur AFTER 'target' was already persisted.
        // Explicit durable readback determines which state can be reported.
        const bool desired_durable=persisted_mode_matches(desired);
        bool old_durable=false;
        if(!desired_durable) {
            old_durable=persisted_mode_matches(previous);
            if(!old_durable) {
                // A partial NVS write is neither the old nor the new mode.
                // Restore the previous set of keys and verify from flash,
                // not just by resetting the in-memory configuration.
                lock();config=previous;++config_generation;unlock();
                save_config();
                old_durable=persisted_mode_matches(previous);
            }
        }
        lock();
        if(desired_durable) {
            config=desired;config_storage_verified=true;
        } else if(old_durable) {
            config=previous;config_storage_verified=true;
        } else {
            // Neither image is durably verified. Keep the requested RAM
            // config visible but explicitly uncertain; physical commands
            // remain blocked until a later verified owner save.
            config=desired;config_storage_verified=false;
        }
        ++config_generation;
        unlock();
        // Success closes the journal. A failed mode NVS write may leave
        // the previous mode intact; roll the site band back as well before
        // returning, otherwise the pending intent would block ALL future heat.
        // Unknown/partial NVS states keep the safe pending marker for reboot.
        const bool finished=desired_durable ?
            site_comfort_transaction_finish(desired) :
            old_durable && site_comfort_transaction_abort_verified(previous);
        if(desired_durable && finished) {
            log_event(3,static_cast<uint8_t>(desired.mode));
        }
        web.send(desired_durable?(finished?200:503):old_durable?500:503,
                 "application/json",
                 desired_durable?(finished?
                     "{\"saved\":true,\"physical_writes\":false}":
                     "{\"saved\":false,\"physical_writes\":false,\"durable_state\":\"journal_pending\"}"):
                 old_durable?(finished?
                     "{\"saved\":false,\"physical_writes\":false,\"restored\":true}":
                     "{\"saved\":false,\"physical_writes\":false,\"durable_state\":\"journal_pending\"}"):
                             "{\"saved\":false,\"physical_writes\":false,\"durable_state\":\"unknown\"}");
    });
    web.on("/api/air/thresholds",HTTP_POST,[&web](){
        if(!cottage_wifi_require_home_lan(web))return;
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
    web.on("/api/air/sensors",HTTP_GET,[&web](){
        if(cottage_wifi_require_home_lan(web)) send_sensors(web);
    });
    web.on("/api/air/sensor-name",HTTP_POST,[&web](){
        if(!cottage_wifi_require_home_lan(web))return;
        if(!check_token(web)) {web.send(403,"text/plain","Token invalid");return;}
        std::array<uint8_t,6> mac{};
        if(!parse_ruuvi_mac(web.arg("mac").c_str(),mac)) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"BAD_MAC\"}");return;
        }
        const String name=web.arg("name");
        lock();
        const SensorNames previous=sensor_names;
        const NameResult r=sensor_names.set(mac,name.c_str());
        if(r==NameResult::BadName||r==NameResult::TableFull||r==NameResult::BadMac) {
            unlock();
            web.send(400,"application/json",r==NameResult::TableFull?
                "{\"saved\":false,\"error\":\"TABLE_FULL\"}":"{\"saved\":false,\"error\":\"BAD_NAME\"}");
            return;
        }
        const bool saved=save_sensor_names(sensor_names);
        if(!saved) sensor_names=previous;     // RAM never runs ahead of flash
        unlock();
        web.send(saved?200:500,"application/json",saved?"{\"saved\":true}":"{\"saved\":false,\"error\":\"NVS\"}");
    });
    web.on("/api/air/bind",HTTP_POST,[&web](){
        if(!cottage_wifi_require_home_lan(web))return;
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
        lock();
        if(kind=="indoor") {
            const bool binding_changed=unbind?indoor_binding.bound():
                (!indoor_binding.bound()||indoor_binding.mac()!=mac);
            if(binding_changed) {
                // Commit order matters: a failed NVS purge must not switch
                // the active sensor in RAM. A subsequent save_config() must
                // never persist a new MAC alongside the old sensor history.
                // Hold the sensor mutex across purge + bind so BLE callbacks
                // cannot refill the old series during the transition.
                Preferences p;
                bool purged=false;
                if(p.begin("cottage_hist",false)) {
                    purged=p.getBytesLength("hourly_v1")==0 ||
                           p.remove("hourly_v1");
                    p.end();
                }
                if(!purged) {
                    unlock();
                    web.send(500,"text/plain","History NVS purge failed");
                    return;
                }
                history_origin_clean=true;
                // Blocking persistence immediately prevents BLE history
                // checkpoint even if save_config later fails or is delayed.
                indoor_binding_persisted=false;
                if(unbind) indoor_binding.unbind();
                else indoor_binding.bind(mac);
                has_indoor=false;indoor_utc=0;indoor_air={};
                alarm_engine=MoistureMonitor(warning_limits);
                history=AirHistory{}; // never mix two sensors in one series
                history_changed=false;
                last_store_ms=uptime_ms();
            }
        } else {
            if(unbind)outdoor_binding.unbind();
            else outdoor_binding.bind(mac);
            has_outdoor=false;outdoor_utc=0;outdoor_air={};
        }
        log_event(4,kind=="indoor"?0:1);
        unlock();
        const bool saved=save_config();
        web.send(saved?200:500,"application/json",
                 saved?"{\"saved\":true,\"physical_writes\":false}":
                       "{\"saved\":false,\"physical_writes\":false}");
    });
    web.on("/api/air/ack",HTTP_POST,[&web](){
        if(!cottage_wifi_require_home_lan(web))return;
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
