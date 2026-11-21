#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <algorithm>
#include <array>
#include <IPAddress.h>
#include <esp_log.h>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <ctime>
#include <cstdlib>
#include <cerrno>
#include <cmath>
#include <cJSON.h>
#include "cottage_midea_service.hpp"
#include "cottage/mode_timing.hpp"
#include <esp_timer.h>
#include "cottage_midea.hpp"
#include "cottage_air_service.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_wifi.hpp"
#include "cottage_network_budget.hpp"
#include "pump/midea_config_migration.hpp"
#include "pump/midea_discovery.hpp"
#include "pump/midea_discovery_info.hpp"
#include "pump/midea_ac_protocol.hpp"

namespace {
using enevina::cottage::MitsubishiModeHistory;
using enevina::midea::AutomaticWriteContext;
// Keep the exact V3 legacy NVS layout readable after upgrading older Lite
// firmware. The new key uses a versioned format and explicit V2/V3 selection.
constexpr uint32_t legacy_magic=0x4d563331; // MV31
constexpr uint32_t config_magic=0x4d563332; // MV32
constexpr uint16_t config_version=2;
struct LegacyConfig {
    uint32_t magic=legacy_magic;
    uint16_t version=1;
    uint16_t size=sizeof(LegacyConfig);
    char host[16]{};
    char device_id[22]{};
    char token_hex[129]{};
    char key_hex[65]{};
};
struct Config {
    uint32_t magic=config_magic;
    uint16_t version=config_version;
    uint16_t size=sizeof(Config);
    char host[16]{};
    char device_id[22]{};
    char token_hex[129]{};
    char key_hex[65]{};
    uint8_t protocol=3; // Explicit protocol, no silent V3->V2 fallback.
    uint8_t reserved[3]{};
};
static_assert(sizeof(LegacyConfig)==240,"Legacy Midea V3 config NVS layout");
Config saved{};
SemaphoreHandle_t mutex=nullptr;
std::atomic<uint32_t> revision{0};
std::atomic<int> last_result{0};
std::atomic<int64_t> last_success{0};
std::atomic<int64_t> last_attempt{0};
std::atomic<bool> polling{false};
std::atomic<TaskHandle_t> poll_task{nullptr};
std::atomic<bool> control_pending{false};
bool control_busy=false; // guarded by mutex
std::atomic<bool> config_updating{false}; // held through NVS persistence
// Per-device, explicit, volatile physical-pilot approval. Never persisted
// and never automatically inherited from read-only credentials or OTA.
struct PilotApproval {
    bool enabled=false;
    char model[48]{};
    char device_id[22]{};
    uint8_t protocol=0;
    uint32_t config_revision=0;
    uint32_t endpoint_generation=0;
    double min_c=0,max_c=0;
};
PilotApproval pilot{}; // guarded by mutex
std::atomic<uint32_t> pending_wifi_generation{0};
std::atomic<uint32_t> pending_endpoint_generation{0};
enevina::midea::ControlPatch pending_patch{}; // guarded by mutex
AutomaticWriteContext pending_automatic{};
bool pending_is_automatic=false;
MitsubishiModeHistory midea_mode_history{};
enevina::cottage::ObservedHeatingRequest observed_request{};
uint32_t automatic_confirmation_generation=0;
int64_t automatic_confirmed_utc=0,automatic_queued_utc=0;
std::atomic<int64_t> control_sent_utc{0};
struct ConfirmedCommand {
    bool available=false,power=false;
    uint8_t mode=0;
    double target_c=0;
};
ConfirmedCommand confirmed_command{}; // guarded by mutex
std::atomic<int> control_result{-1};
std::atomic<int64_t> control_attempt{0};
std::atomic<int64_t> control_success{0};
std::atomic<int64_t> control_queued_utc{0};
enevina::midea::State observed{};
uint32_t observed_wifi_generation=0,observed_endpoint_generation=0;
bool configured=false;
bool is_midea_selected() {
    const auto s=site_settings_snapshot();
    return std::strcmp(s.pump_adapter,"midea_lan")==0;
}
bool decimal_u8(const String& text,uint8_t& value) {
    if(text.isEmpty())return false;
    for(size_t i=0;i<text.length();++i)
        if(text[i]<'0'||text[i]>'9')return false;
    errno=0;
    char* end=nullptr;
    const unsigned long parsed=std::strtoul(text.c_str(),&end,10);
    if(errno || !end || *end || parsed>255)return false;
    value=static_cast<uint8_t>(parsed);
    return true;
}
bool strict_temperature(const String& text,double& value) {
    if(text.isEmpty())return false;
    errno=0;
    char* end=nullptr;
    const double result=std::strtod(text.c_str(),&end);
    if(errno || !end || *end || !std::isfinite(result))return false;
    value=result;
    return true;
}
bool private_ipv4(const char* host) {
    if(!host)return false;
    IPAddress ip;
    if(!ip.fromString(host))return false;
    const uint8_t a=ip[0],b=ip[1];
    return a==10 || (a==172 && b>=16 && b<=31) ||
           (a==192 && b==168);
}
bool valid_config(const Config& c) {
    if(c.magic!=config_magic || c.version!=config_version || c.size!=sizeof(Config) ||
       !memchr(c.host,0,sizeof(c.host)) ||
       !memchr(c.device_id,0,sizeof(c.device_id)) ||
       !memchr(c.token_hex,0,sizeof(c.token_hex)) ||
       !memchr(c.key_hex,0,sizeof(c.key_hex)) ||
       !private_ipv4(c.host) ||
       (c.protocol!=2 && c.protocol!=3) ||
       c.reserved[0] || c.reserved[1] || c.reserved[2])return false;
    uint64_t id=0;
    if(!enevina::midea::decode_device_id(c.device_id,id))return false;
    if(c.protocol==2) {
        // A V2 client never reads V3 credentials; reject mixed-mode records.
        return c.token_hex[0]==0 && c.key_hex[0]==0;
    }
    enevina::midea::Credential credentials{};
    return enevina::midea::decode_credentials(
        c.device_id,c.token_hex,c.key_hex,credentials);
}
bool read_legacy_v3(Preferences& preferences,Config& out) {
    LegacyConfig older{};
    const bool legacy=preferences.getBytesLength("config_v1")==sizeof(older) &&
        preferences.getBytes("config_v1",&older,sizeof(older))==sizeof(older) &&
        older.magic==legacy_magic && older.version==1 &&
        older.size==sizeof(older) &&
        memchr(older.host,0,sizeof(older.host)) &&
        memchr(older.device_id,0,sizeof(older.device_id)) &&
        memchr(older.token_hex,0,sizeof(older.token_hex)) &&
        memchr(older.key_hex,0,sizeof(older.key_hex));
    if(!legacy)return false;
    Config migrated{};
    std::memcpy(migrated.host,older.host,sizeof(older.host));
    std::memcpy(migrated.device_id,older.device_id,sizeof(older.device_id));
    std::memcpy(migrated.token_hex,older.token_hex,sizeof(older.token_hex));
    std::memcpy(migrated.key_hex,older.key_hex,sizeof(older.key_hex));
    migrated.protocol=3;
    if(!valid_config(migrated))return false;
    out=migrated;
    return true;
}
bool load_config(Config& dest) {
    Preferences preferences;
    if(!preferences.begin("midea_v3",true))return false;
    // A corrupt new key must never resurrect an intentionally retired V3
    // configuration. Only consider legacy data when v2 is either absent,
    // or valid AND explicitly set to protocol 3.
    bool ok=false;
    if(preferences.isKey("config_v2")) {
        ok=preferences.getBytesLength("config_v2")==sizeof(dest) &&
           preferences.getBytes("config_v2",&dest,sizeof(dest))==sizeof(dest) &&
           valid_config(dest);
        if(ok && dest.protocol==3) {
            Config rollback_settings{};
            const bool valid_legacy=read_legacy_v3(preferences,rollback_settings);
            // The old V3 image writes config_v1 only. After an OTA rollback,
            // later edits on that image must take precedence on re-upgrade.
            // If V2 was explicitly selected, this path is never entered.
            const enevina::midea::V3SettingsView current{
                dest.host,dest.device_id,dest.token_hex,dest.key_hex};
            const enevina::midea::V3SettingsView older{
                rollback_settings.host,rollback_settings.device_id,
                rollback_settings.token_hex,rollback_settings.key_hex};
            if(enevina::midea::rollback_v3_settings_changed(
                    dest.protocol,valid_legacy,current,older))
                dest=rollback_settings;
        }
    } else {
        ok=read_legacy_v3(preferences,dest);
    }
    preferences.end();
    return ok;
}
bool persist_config(const Config& c) {
    Preferences preferences;
    if(!preferences.begin("midea_v3",false))return false;
    if(c.protocol==3) {
        // Keep an up-to-date, rollback-readable copy for older V3 firmware
        // which knows ONLY config_v1. The OTA A/B previous image must still
        // see valid V3 credentials after an upgrade and settings save.
        LegacyConfig fallback{};
        std::memcpy(fallback.host,c.host,sizeof(fallback.host));
        std::memcpy(fallback.device_id,c.device_id,sizeof(fallback.device_id));
        std::memcpy(fallback.token_hex,c.token_hex,sizeof(fallback.token_hex));
        std::memcpy(fallback.key_hex,c.key_hex,sizeof(fallback.key_hex));
        if(preferences.putBytes("config_v1",&fallback,sizeof(fallback))
           !=sizeof(fallback)) {
            preferences.end();
            return false;
        }
    }
    // If switching to V2, retain any earlier config_v1 V3 record as the
    // previous firmware's read-only fallback. Old V3 firmware cannot read
    // a V2 config, so intentionally do not erase the rollback record.
    const bool ok=preferences.putBytes("config_v2",&c,sizeof(c))==sizeof(c);
    preferences.end();
    return ok;
}
const char* read_code(int status) {
    using R=enevina::midea::ReadStatus;
    switch(static_cast<R>(status)) {
        case R::Ok: return "ok";
        case R::InvalidAddress: return "invalid_address";
        case R::InvalidCredentials: return "invalid_credentials";
        case R::ConnectionFailed: return "connection_failed";
        case R::AuthenticationFailed: return "authentication_failed";
        case R::ProtocolFailed: return "protocol_failed";
        case R::UnsupportedState: return "unsupported_state";
        case R::TimeNotReady: return "time_not_ready";
        case R::ControlNotConfirmed: return "control_not_confirmed";
        case R::ResourceBusy: return "resource_busy";
    }
    return "not_tested";
}
struct BooleanControl {
    const char* name;
    uint32_t field;
    enevina::midea::AcFeature feature;
    bool enevina::midea::State::*value;
    bool enevina::midea::ControlPatch::*patch;
};
constexpr BooleanControl boolean_controls[]={
    {"eco",enevina::midea::ControlEco,enevina::midea::AcFeature::Eco,&enevina::midea::State::eco,&enevina::midea::ControlPatch::eco},
    {"turbo",enevina::midea::ControlTurbo,enevina::midea::AcFeature::TurboCool,&enevina::midea::State::turbo,&enevina::midea::ControlPatch::turbo},
    {"sleep",enevina::midea::ControlSleep,enevina::midea::AcFeature::Sleep,&enevina::midea::State::sleep,&enevina::midea::ControlPatch::sleep},
    {"freeze_protection",enevina::midea::ControlFreezeProtection,enevina::midea::AcFeature::FreezeProtection,&enevina::midea::State::freeze_protection,&enevina::midea::ControlPatch::freeze_protection},
    {"display",enevina::midea::ControlDisplay,enevina::midea::AcFeature::Display,&enevina::midea::State::display,&enevina::midea::ControlPatch::display},
    {"anion",enevina::midea::ControlAnion,enevina::midea::AcFeature::Anion,&enevina::midea::State::purifier,&enevina::midea::ControlPatch::anion},
    {"breezeless",enevina::midea::ControlBreezeless,enevina::midea::AcFeature::Breezeless,&enevina::midea::State::breezeless,&enevina::midea::ControlPatch::breezeless},
    {"indirect_wind",enevina::midea::ControlIndirectWind,enevina::midea::AcFeature::IndirectWind,&enevina::midea::State::indirect_wind,&enevina::midea::ControlPatch::indirect_wind},
    {"self_clean",enevina::midea::ControlSelfClean,enevina::midea::AcFeature::SelfClean,&enevina::midea::State::self_clean,&enevina::midea::ControlPatch::self_clean},
    {"sound",enevina::midea::ControlSound,enevina::midea::AcFeature::Sound,&enevina::midea::State::sound,&enevina::midea::ControlPatch::sound}
};
struct NumericControl {
    const char* name;
    uint32_t field;
    enevina::midea::AcFeature feature;
    uint8_t enevina::midea::State::*value;
    uint8_t enevina::midea::ControlPatch::*patch;
};
constexpr NumericControl numeric_controls[]={
    {"vertical_angle",enevina::midea::ControlVerticalAngle,enevina::midea::AcFeature::VerticalAngle,&enevina::midea::State::vertical_angle,&enevina::midea::ControlPatch::vertical_angle},
    {"horizontal_angle",enevina::midea::ControlHorizontalAngle,enevina::midea::AcFeature::HorizontalAngle,&enevina::midea::State::horizontal_angle,&enevina::midea::ControlPatch::horizontal_angle},
    {"rate_select",enevina::midea::ControlRateSelect,enevina::midea::AcFeature::RateSelect,&enevina::midea::State::rate_select,&enevina::midea::ControlPatch::rate_select}
};
bool control_patch_fields_valid(const enevina::midea::ControlPatch& patch) {
    constexpr uint32_t fields=enevina::midea::AcGeneralFields|enevina::midea::AcPropertyFields;
    return patch.changed && !(patch.changed&~fields);
}
bool observed_control_field(const enevina::midea::State& state,uint32_t field) {
    // An unsupported feature may safely be encoded as off, but capability
    // inference is not a live observation to publish as a device setting.
    return (state.properties_known&field)!=0 &&
        !(field==enevina::midea::ControlFreezeProtection && state.freeze_protection_inferred);
}
bool optional_control_supported(const enevina::midea::State& state,
                                uint32_t field,enevina::midea::AcFeature feature) {
    using namespace enevina::midea;
    const auto& caps=state.profile.capabilities;
    if(field==ControlTurbo)feature=state.mode==4?AcFeature::TurboHeat:AcFeature::TurboCool;
    // An explicit negative report wins over a previously observed value.
    bool supported=caps.reports(feature)?caps.supports(feature):
        observed_control_field(state,field);
    if(field==ControlRateSelect &&
       (caps.supports(AcFeature::RateSelect2) || caps.supports(AcFeature::RateSelect5)))supported=true;
    if(!supported)return false;
    // The device may advertise a feature whose command is not implemented for
    // its message family. Use the same pure field validator as the API writer.
    ControlPatch probe{};probe.changed=field;
    if(field==ControlVerticalAngle)probe.vertical_angle=25;
    if(field==ControlHorizontalAngle)probe.horizontal_angle=25;
    if(field==ControlRateSelect) {
        for(const uint8_t value:{1u,20u,40u,50u,60u,75u,80u,100u}) {
            probe.rate_select=value;
            if(valid_ac_control_patch(state,probe))return true;
        }
        return false;
    }
    return valid_ac_control_patch(state,probe);
}
const char* control_request_error(const enevina::midea::State& state,
                                  const enevina::midea::ControlPatch& patch) {
    using namespace enevina::midea;
    if((patch.changed&AcGeneralFields) && (patch.changed&AcPropertyFields))
        return "MIXED_COMMAND_FAMILIES";
    if(!valid_ac_control_patch(state,patch))return "UNSUPPORTED_CONTROL";
    Bytes encoded;
    if(ac_set_state_command(state,patch,2,encoded))return nullptr;
    return "UNSUPPORTED_STATE";
}
bool mode_control_available(const enevina::midea::State& state,uint8_t mode) {
    using namespace enevina::midea;
    const auto& caps=state.profile.capabilities;
    if(!mode_supported(caps,mode))return false;
    const auto range=temperature_range(caps,mode);
    const double step=temperature_step(caps);
    if(!std::isfinite(step) || step<=0 ||
       !std::isfinite(range.minimum_c) || !std::isfinite(range.maximum_c))return false;
    // Availability asks whether the destination mode has any encodable target,
    // not whether it can inherit this mode's target. This pure probe never
    // changes the observed state or relaxes validation of a submitted draft.
    ControlPatch probe{};probe.changed=ControlMode|ControlTemperature;probe.mode=mode;
    probe.setpoint_c=std::ceil(std::fmax(12.0,range.minimum_c)/step-0.00001)*step;
    if(probe.setpoint_c>range.maximum_c)return false;
    if(valid_ac_control_patch(state,probe))return true;
    // Leaving Dry normally selects Auto. Devices without Auto can still
    // change mode by supplying an explicit supported fan in the same command.
    probe.changed|=ControlFan;
    for(unsigned fan=0;fan<=102;++fan) {
        probe.fan=static_cast<uint8_t>(fan);
        if(fan_supported(caps,probe.fan) && valid_ac_control_patch(state,probe))return true;
    }
    return false;
}
bool add_automatic_heat(const enevina::midea::State& state,enevina::midea::ControlPatch& patch) {
    using namespace enevina::midea;
    patch.changed|=ControlMode|ControlPower;
    patch.mode=4;patch.power=true;
    const auto& caps=state.profile.capabilities;
    if(state.mode!=3 || fan_supported(caps,102))return true;
    // Leaving Dry normally selects Auto. When the model does not support
    // Auto, prefer the already observed speed before falling back to one
    // of the documented native speed values.
    for(const uint8_t speed:{state.fan,uint8_t{40},uint8_t{60},uint8_t{80},
                            uint8_t{20},uint8_t{100}}) {
        if(fan_supported(caps,speed)) {
            patch.changed|=ControlFan;
            patch.fan=speed;
            return true;
        }
    }
    return false;
}
bool pilot_current_locked(uint32_t endpoint) {
    return pilot.enabled && pilot.config_revision==revision.load() &&
        pilot.endpoint_generation==endpoint && pilot.protocol==saved.protocol &&
        !std::strcmp(pilot.device_id,saved.device_id);
}
bool fresh_locked(int64_t now,uint32_t link,uint32_t endpoint) {
    const auto stamp=last_success.load();
    return configured && !config_updating.load() && WiFi.status()==WL_CONNECTED &&
        stamp>0 && now>=stamp && now-stamp<=90 &&
        observed_wifi_generation==link && observed_endpoint_generation==endpoint;
}
enevina::PumpRoomState room_state_locked(int64_t now) {
    using namespace enevina;
    using namespace enevina::midea;
    PumpRoomState out{};
    const auto link=cottage_wifi_link_generation();
    const auto endpoint=site_settings_endpoint_generation();
    if(!is_midea_selected() || !fresh_locked(now,link,endpoint))return out;
    out.protocol=saved.protocol==2?PumpRoomProtocol::MideaV2Ac:PumpRoomProtocol::MideaV3Ac;
    out.type=HeatPumpType::AirToAir;
    out.endpoint_generation=endpoint;
    out.configuration_generation=revision.load();
    out.link_generation=link;
    out.source=PumpStateSource::DirectLan;
    // The completed paired LAN exchange is fresh. The module did not report
    // a pump-origin timestamp for the underlying sensor measurements.
    out.evidence.received_utc=last_success.load();
    out.mode=!observed.power?PumpRoomMode::Off:
        observed.mode==4?PumpRoomMode::Heat:PumpRoomMode::Other;
    out.setpoint_c=observed.setpoint_c;
    out.request_generation=observed_request.generation();
    const uint64_t monotonic_ms=static_cast<uint64_t>(esp_timer_get_time()/1000);
    out.mode_minutes=observed_request.mode_minutes(monotonic_ms);
    out.request_minutes=observed_request.request_minutes(monotonic_ms);
    if(automatic_confirmation_generation==out.request_generation) {
        out.automatic_confirmed_utc=automatic_confirmed_utc;
        out.automatic_queued_utc=automatic_queued_utc;
    }
    if(observed.has_indoor)out.indoor_c=observed.indoor_c;
    if(observed.has_outdoor)out.outdoor_c=observed.outdoor_c;
    const auto& caps=observed.profile.capabilities;
    const auto range=temperature_range(caps,4);
    out.heating={range.minimum_c,range.maximum_c,temperature_step(caps),
                 mode_supported(caps,4),true};
    if(pilot_current_locked(endpoint)) {
        out.heating.minimum_c=std::max(out.heating.minimum_c,pilot.min_c);
        out.heating.maximum_c=std::min(out.heating.maximum_c,pilot.max_c);
    }
    return out;
}
// mutex held: both manual and automatic submit to this single worker queue.
void queue_locked(const enevina::midea::ControlPatch& patch,int64_t now,
                  uint32_t link,uint32_t endpoint,const AutomaticWriteContext* automatic) {
    pending_patch=patch;
    pending_is_automatic=automatic!=nullptr;
    pending_automatic=automatic?*automatic:AutomaticWriteContext{};
    pending_wifi_generation.store(link);
    pending_endpoint_generation.store(endpoint);
    control_queued_utc.store(now);
    control_sent_utc.store(0);
    control_result.store(-1);
    control_pending.store(true);
}

void add_profile_status(cJSON* root,const enevina::midea::State& reading,bool fresh) {
    using namespace enevina::midea;
    const auto& caps=reading.profile.capabilities;
    cJSON* profile=cJSON_AddObjectToObject(root,"profile");
    cJSON_AddStringToObject(profile,"family",reading.profile.family==AcProtocol::Subprotocol?"subprotocol":"legacy");
    cJSON_AddNumberToObject(profile,"protocol_version",reading.profile.protocol_version);
    cJSON_AddBoolToObject(profile,"subtype_known",reading.profile.subtype_known);
    cJSON_AddNumberToObject(profile,"subtype",reading.profile.subtype);
    cJSON_AddBoolToObject(profile,"model_known",reading.profile.model_known);
    cJSON_AddStringToObject(profile,"model",reading.profile.model_known?reading.profile.model.data():"");
    cJSON* features=cJSON_AddObjectToObject(root,"capabilities");
    cJSON_AddStringToObject(features,"source",caps.received?"reported":"default");
    cJSON_AddBoolToObject(features,"reported",caps.received);
    cJSON_AddNumberToObject(features,"temperature_step",temperature_step(caps));
    cJSON* modes=cJSON_AddArrayToObject(features,"modes");
    cJSON* fans=cJSON_AddArrayToObject(features,"fans");
    cJSON* swings=cJSON_AddArrayToObject(features,"swings");
    cJSON* limits=cJSON_AddObjectToObject(features,"temp_limits");
    for(uint8_t mode=1;mode<=5;++mode) {
        if(mode_control_available(reading,mode))
            cJSON_AddItemToArray(modes,cJSON_CreateNumber(mode));
        const auto range=temperature_range(caps,mode);
        char key[2]={static_cast<char>('0'+mode),0};
        cJSON* item=cJSON_AddObjectToObject(limits,key);
        cJSON_AddBoolToObject(item,"known",range.known);
        cJSON_AddNumberToObject(item,"min_c",range.minimum_c);
        cJSON_AddNumberToObject(item,"max_c",range.maximum_c);
    }
    for(unsigned fan=0;fan<=102;++fan) {
        ControlPatch probe{};probe.changed=ControlFan;probe.fan=static_cast<uint8_t>(fan);
        if(fan_supported(caps,probe.fan) && valid_ac_control_patch(reading,probe))
            cJSON_AddItemToArray(fans,cJSON_CreateNumber(fan));
    }
    for(const uint8_t swing:{0u,3u,12u,15u}) {
        ControlPatch probe{};probe.changed=ControlSwing;probe.swing=swing;
        if(swing_supported(caps,swing) && valid_ac_control_patch(reading,probe))
            cJSON_AddItemToArray(swings,cJSON_CreateNumber(swing));
    }
    cJSON* controls=cJSON_AddArrayToObject(features,"controls");
    cJSON* options=cJSON_AddObjectToObject(features,"property_options");
    cJSON* known=cJSON_AddArrayToObject(root,"state_known");
    constexpr uint32_t core_fields[]={ControlPower,ControlMode,ControlTemperature,ControlFan,ControlSwing};
    const char* core_names[]={"power","mode","target_c","fan","swing"};
    for(size_t index=0;index<5;++index) {
        bool available=true;
        if(core_fields[index]==ControlMode)available=cJSON_GetArraySize(modes)>0;
        if(core_fields[index]==ControlFan)available=cJSON_GetArraySize(fans)>0;
        if(core_fields[index]==ControlSwing)available=cJSON_GetArraySize(swings)>0;
        if(available)cJSON_AddItemToArray(controls,cJSON_CreateString(core_names[index]));
        if(fresh && observed_control_field(reading,core_fields[index]))
            cJSON_AddItemToArray(known,cJSON_CreateString(core_names[index]));
    }
    cJSON* policy=cJSON_AddObjectToObject(root,"control_policy");
    cJSON* writable=cJSON_AddArrayToObject(policy,"property_controls");
    bool property_available=false;
    for(const auto& field:boolean_controls) {
        const bool supported=optional_control_supported(reading,field.field,field.feature);
        if(supported)cJSON_AddItemToArray(controls,cJSON_CreateString(field.name));
        const bool has_value=fresh && observed_control_field(reading,field.field);
        if(has_value) {
            cJSON_AddItemToArray(known,cJSON_CreateString(field.name));
            cJSON_AddBoolToObject(root,field.name,reading.*(field.value));
        } else cJSON_AddNullToObject(root,field.name);
        if(supported && (field.field&AcPropertyFields)) {
            ControlPatch probe{};probe.changed=field.field;probe.*(field.patch)=reading.*(field.value);
            if(fresh && !control_request_error(reading,probe)) {
                property_available=true;
                cJSON_AddItemToArray(writable,cJSON_CreateString(field.name));
            }
        }
    }
    for(const auto& field:numeric_controls) {
        const bool supported=optional_control_supported(reading,field.field,field.feature);
        if(supported)cJSON_AddItemToArray(controls,cJSON_CreateString(field.name));
        const bool has_value=fresh && observed_control_field(reading,field.field);
        if(has_value) {
            cJSON_AddItemToArray(known,cJSON_CreateString(field.name));
            cJSON_AddNumberToObject(root,field.name,reading.*(field.value));
        } else cJSON_AddNullToObject(root,field.name);
        cJSON* choices=cJSON_AddArrayToObject(options,field.name);
        bool writable_field=false;
        if(supported)for(unsigned value=0;value<=100;++value) {
            ControlPatch probe{};probe.changed=field.field;probe.*(field.patch)=static_cast<uint8_t>(value);
            if(!valid_ac_control_patch(reading,probe))continue;
            cJSON_AddItemToArray(choices,cJSON_CreateNumber(value));
            if(fresh && !control_request_error(reading,probe))writable_field=true;
        }
        if(writable_field) {
            property_available=true;
            cJSON_AddItemToArray(writable,cJSON_CreateString(field.name));
        }
    }
    ControlPatch probe{};probe.changed=ControlPower;probe.power=reading.power;
    const char* blocked=fresh?control_request_error(reading,probe):"STALE_STATE";
    const bool requires_freeze=blocked && !std::strcmp(blocked,"MISSING_FROST_STATE");
    cJSON_AddBoolToObject(policy,"general_available",blocked==nullptr);
    cJSON_AddBoolToObject(policy,"property_available",property_available);
    cJSON_AddBoolToObject(policy,"requires_freeze_choice",requires_freeze &&
        optional_control_supported(reading,ControlFreezeProtection,AcFeature::FreezeProtection));
    cJSON_AddStringToObject(policy,"general_block",!blocked?"":requires_freeze?"missing_frost_state":
                           !fresh?"stale_state":"unsupported_state");
}
void send_status(WebServer& web) {
    const bool selected=is_midea_selected();
    enevina::midea::State reading{};
    bool stored=false,busy=false,writing=false,pilot_ok=false;
    uint8_t selected_protocol=3;
    int64_t last=0,attempted=0,requested_utc=0,sent_utc=0,confirmed_utc=0;
    int status=-1,write_status=-1;
    uint32_t sample_link=0,sample_endpoint=0;
    double pilot_min=0,pilot_max=0;
    enevina::midea::ControlPatch requested{};
    ConfirmedCommand confirmed{};
    // State, capability profile and their identity are one atomic snapshot.
    xSemaphoreTake(mutex,portMAX_DELAY);
    reading=observed;stored=configured;selected_protocol=saved.protocol;
    last=last_success.load();status=last_result.load();attempted=last_attempt.load();
    busy=polling.load();writing=control_busy;
    requested=pending_patch;confirmed=confirmed_command;
    requested_utc=control_queued_utc.load();sent_utc=control_sent_utc.load();
    confirmed_utc=control_success.load();write_status=control_result.load();
    sample_link=observed_wifi_generation;sample_endpoint=observed_endpoint_generation;
    pilot_ok=selected && pilot.enabled && pilot.config_revision==revision.load() &&
            pilot.endpoint_generation==site_settings_endpoint_generation() &&
            pilot.protocol==saved.protocol &&
            !std::strcmp(pilot.device_id,saved.device_id) && !config_updating;
    if(pilot_ok) {pilot_min=pilot.min_c;pilot_max=pilot.max_c;}
    xSemaphoreGive(mutex);
    const auto now=static_cast<int64_t>(time(nullptr));
    const bool fresh=selected && stored && last>0 && now>=last && now-last<=90 &&
        WiFi.status()==WL_CONNECTED && sample_link==cottage_wifi_link_generation() &&
        sample_endpoint==site_settings_endpoint_generation();
    const char* code=!selected?"not_selected":
        !stored?"not_configured":attempted<=0?"not_tested":read_code(status);
    cJSON* root=cJSON_CreateObject();
    if(!root){web.send(503,"application/json","{}");return;}
    cJSON_AddBoolToObject(root,"selected",selected);
    cJSON_AddBoolToObject(root,"configured",stored);
    cJSON_AddBoolToObject(root,"fresh",fresh);
    cJSON_AddNumberToObject(root,"protocol",selected_protocol);
    cJSON_AddBoolToObject(root,"read_only",false);
    cJSON_AddBoolToObject(root,"manual_control",true);
    cJSON_AddBoolToObject(root,"automatic_enabled",cottage_midea_automatic_enabled());
    cJSON_AddBoolToObject(root,"pilot_approved",pilot_ok);
    cJSON_AddNumberToObject(root,"pilot_min_c",pilot_min);
    cJSON_AddNumberToObject(root,"pilot_max_c",pilot_max);
    cJSON_AddBoolToObject(root,"control_pending",control_pending.load());
    cJSON_AddBoolToObject(root,"control_busy",writing);
    cJSON_AddNumberToObject(root,"control_last_attempt_utc",control_attempt.load());
    cJSON_AddNumberToObject(root,"control_last_sent_utc",sent_utc);
    cJSON_AddNumberToObject(root,"control_last_requested_utc",requested_utc);
    using namespace enevina::midea;
    if(auto* request=cJSON_AddObjectToObject(root,"control_requested")) {
        if(requested.changed&ControlPower)cJSON_AddBoolToObject(request,"power",requested.power);
        if(requested.changed&ControlMode)cJSON_AddNumberToObject(request,"mode",requested.mode);
        if(requested.changed&ControlTemperature)cJSON_AddNumberToObject(request,"target_c",requested.setpoint_c);
    }
    if(confirmed.available) {
        if(auto* result=cJSON_AddObjectToObject(root,"control_confirmed")) {
            cJSON_AddBoolToObject(result,"power",confirmed.power);
            cJSON_AddNumberToObject(result,"mode",confirmed.mode);
            cJSON_AddNumberToObject(result,"target_c",confirmed.target_c);
        }
    }
    cJSON_AddNumberToObject(root,"control_last_confirmed_utc",confirmed_utc);
    cJSON_AddStringToObject(root,"control_code",write_status<0?"not_requested":read_code(write_status));
    cJSON_AddNumberToObject(root,"last_poll_utc",fresh?last:0);
    cJSON_AddNumberToObject(root,"last_attempt_utc",attempted>0?attempted:0);
    cJSON_AddBoolToObject(root,"polling",busy);
    cJSON_AddStringToObject(root,"read_code",code);
    cJSON_AddNumberToObject(root,"read_status",status);
    cJSON_AddNumberToObject(root,"read_detail",selected && selected_protocol==3?enevina::midea::last_v3_read_detail():0);
    cJSON_AddNumberToObject(root,"read_opcode",selected && selected_protocol==3?enevina::midea::last_v3_reply_opcode():0);
    cJSON_AddBoolToObject(root,"power",fresh && reading.power);
    cJSON_AddNumberToObject(root,"mode",fresh?reading.mode:255);
    cJSON_AddNumberToObject(root,"target_c",fresh?reading.setpoint_c:0);
    cJSON_AddNumberToObject(root,"fan",fresh?reading.fan:0);
    cJSON_AddNumberToObject(root,"swing",fresh?reading.swing:0);
    cJSON_AddBoolToObject(root,"indoor_available",fresh && reading.has_indoor);
    cJSON_AddNumberToObject(root,"indoor_c",fresh && reading.has_indoor?reading.indoor_c:0);
    cJSON_AddBoolToObject(root,"outdoor_available",fresh && reading.has_outdoor);
    cJSON_AddNumberToObject(root,"outdoor_c",fresh && reading.has_outdoor?reading.outdoor_c:0);
    add_profile_status(root,reading,fresh);
    char* data=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    web.sendHeader("Cache-Control","no-store");
    web.send(data?200:503,"application/json; charset=utf-8",data?data:"{}");
    if(data)cJSON_free(data);
}
} // namespace

bool cottage_midea_selected() {
    return is_midea_selected();
}
bool cottage_midea_automatic_ready() {
    if(!mutex || !is_midea_selected())return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const auto snapshot=room_state_locked(time(nullptr));
    const bool ready=pilot_current_locked(snapshot.endpoint_generation) &&
        snapshot.source==enevina::PumpStateSource::DirectLan &&
        snapshot.heating.heating && snapshot.heating.minimum_c<=snapshot.heating.maximum_c &&
        !control_pending.load() && !control_busy && poll_task.load();
    xSemaphoreGive(mutex);
    return ready;
}
enevina::PumpRoomState cottage_midea_room_state() {
    if(!mutex)return {};
    xSemaphoreTake(mutex,portMAX_DELAY);
    const auto snapshot=room_state_locked(time(nullptr));
    xSemaphoreGive(mutex);
    return snapshot;
}
enevina::cottage::ControlBlock cottage_midea_submit_automatic(
        const enevina::PumpRoomState& expected,enevina::cottage::ControlSnapshot decision,
        uint32_t authorization_generation,uint32_t config_generation,uint32_t site_generation) {
    using namespace enevina;
    using namespace enevina::cottage;
    using namespace enevina::midea;
    if(!mutex || !cottage_midea_automatic_write_current(
          authorization_generation,config_generation,site_generation))return ControlBlock::Disabled;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const auto now=static_cast<int64_t>(time(nullptr));
    const auto current=room_state_locked(now);
    const auto task=poll_task.load();
    const auto offer=[&]() {
        if(!task || control_busy || control_pending.load() ||
           current.source!=PumpStateSource::DirectLan ||
           current.protocol!=expected.protocol ||
           current.endpoint_generation!=expected.endpoint_generation ||
           current.configuration_generation!=expected.configuration_generation ||
           current.link_generation!=expected.link_generation ||
           current.mode!=expected.mode || current.request_generation!=expected.request_generation ||
           std::fabs(current.setpoint_c-expected.setpoint_c)>0.001 ||
           !pilot_current_locked(current.endpoint_generation) ||
           (current.mode!=PumpRoomMode::Off && current.mode!=PumpRoomMode::Heat))
            return ControlBlock::InputsUnverified;
        decision.current_heat=current.mode==PumpRoomMode::Heat;
        decision.current_setpoint_c=current.setpoint_c;
        decision.cn105_link_fresh=decision.bridge_get_ok=true;
        decision.ms_since_mode_change=midea_mode_history.since_change(millis());
        const auto proposed=decision.proposal;
        if(decision.proposal.action==BootstrapAction::HeatSetpoint) {
            const auto& caps=current.heating;
            const double lo=std::ceil(caps.minimum_c/caps.step_c)*caps.step_c;
            const double hi=std::floor(caps.maximum_c/caps.step_c)*caps.step_c;
            if(!std::isfinite(lo) || !std::isfinite(hi) || lo>hi)
                return ControlBlock::UnsupportedCommand;
            const double wanted=decision.proposal.heat_setpoint_c;
            if(!std::isfinite(wanted))return ControlBlock::UnsupportedCommand;
            decision.proposal.heat_setpoint_c=std::clamp(
                std::round(wanted/caps.step_c)*caps.step_c,lo,hi);
        }
        const auto command=choose_pump_control(decision,current.heating,HeatTransition::Combined);
        if(command.step==ControlStep::None)return command.gate;
        ControlPatch patch{};
        if(command.step==ControlStep::SetOff) {
            patch.changed=ControlPower;patch.power=false;
        } else {
            patch.changed=ControlTemperature;
            patch.setpoint_c=command.target_c;
            if(command.step==ControlStep::SetHeat && !add_automatic_heat(observed,patch))
                return ControlBlock::UnsupportedCommand;
        }
        if(control_request_error(observed,patch))return ControlBlock::UnsupportedCommand;
        const AutomaticWriteContext context{authorization_generation,config_generation,
            site_generation,observed.power,observed.mode,observed.setpoint_c,
            proposed.action,proposed.heat_setpoint_c};
        queue_locked(patch,now,current.link_generation,current.endpoint_generation,&context);
        return ControlBlock::Ready;
    };
    const auto gate=offer();
    xSemaphoreGive(mutex);
    if(gate==ControlBlock::Ready)xTaskNotifyGive(task);
    return gate;
}

uint32_t cottage_midea_configuration_generation() {
    return revision.load();
}
uint32_t cottage_midea_calibration_identity() {
    if(!mutex)return 0;
    xSemaphoreTake(mutex,portMAX_DELAY);
    uint32_t identity=0;
    if(configured) {
        identity=2166136261u^saved.protocol;
        for(const char* field:{saved.device_id,saved.host}) {
            for(;*field;++field) {
                identity^=static_cast<uint8_t>(*field);
                identity*=16777619u;
            }
            identity^=0xffu;
            identity*=16777619u;
        }
    }
    xSemaphoreGive(mutex);
    return identity;
}
bool cottage_midea_configuration_current(uint32_t expected_generation) {
    return !config_updating.load() && revision.load()==expected_generation;
}
void cottage_midea_link_disconnected() {
    // Invoked from Arduino Wi-Fi event task. No mutex/NVS or network calls.
    // Queued physical writes must never survive a Wi-Fi disconnect/reconnect.
    control_pending.store(false);
    pending_wifi_generation.store(0);
    pending_endpoint_generation.store(0);
    control_result.store(static_cast<int>(
       enevina::midea::ReadStatus::ResourceBusy));
    last_success.store(0);
}
void cottage_midea_service_initialize() {
    mutex=xSemaphoreCreateMutex();
    if(!mutex)abort();
    Config candidate{};
    if(load_config(candidate)) {
        saved=candidate;
        configured=true;
    }
}
void cottage_midea_service_register_routes(WebServer& web) {
    web.on("/api/midea/status",HTTP_GET,[&web](){if(cottage_wifi_require_home_lan(web))send_status(web);});
    web.on("/api/midea/discover",HTTP_POST,[&web](){
        // Discovery is an explicit owner action on trusted home LAN, not the
        // open setup AP. Existing per-boot CSRF token, no new login or PIN.
        if(!cottage_wifi_require_home_lan(web))return;
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
               web.arg("token").c_str())) {
            web.send(403,"application/json","{\"error\":\"TOKEN\"}");
            return;
        }
        if(cottage_ota_network_maintenance()) {
            web.send(503,"application/json","{\"error\":\"OTA_BUSY\"}");
            return;
        }
        // One bounded UDP discovery pass. It cannot enroll a device, write
        // NVS, authenticate against a cloud, or send an HVAC command.
        enevina_firmware::NetworkBudgetLease lease(100,64*1024,16*1024);
        if(!lease.held()) {
            web.send(503,"application/json","{\"error\":\"NETWORK_BUSY\"}");
            return;
        }
        WiFiUDP udp;
        if(!udp.begin(0)) {
            web.send(503,"application/json","{\"error\":\"UDP\"}");
            return;
        }
        const auto ip=WiFi.localIP();
        const auto mask=WiFi.subnetMask();
        const IPAddress directed(
            static_cast<uint8_t>(ip[0] | ~mask[0]),
            static_cast<uint8_t>(ip[1] | ~mask[1]),
            static_cast<uint8_t>(ip[2] | ~mask[2]),
            static_cast<uint8_t>(ip[3] | ~mask[3]));
        // Both Midea discovery ports. 255.255.255.255 works on LANs which
        // reject directed broadcast. Failed sends do not create a false hit.
        const IPAddress broadcasts[2]={{255,255,255,255},directed};
        for(const auto& dest:broadcasts)
            for(const uint16_t port:{6445u,20086u}) {
                if(udp.beginPacket(dest,port)) {
                    udp.write(enevina::midea::discovery_request.data(),
                              enevina::midea::discovery_request.size());
                    udp.endPacket();
                }
            }
        struct Hit {
            IPAddress ip;uint64_t id;uint8_t protocol;
            bool metadata_known=false;
            enevina::midea::DiscoveryInfo info{};
        };
        std::array<Hit,8> hits{};
        size_t count=0;
        bool truncated=false;
        const uint32_t begin=millis();
        std::array<uint8_t,512> reply{};
        while(static_cast<uint32_t>(millis()-begin)<1350u) {
            const int packet_size=udp.parsePacket();
            if(packet_size<1) {delay(15);continue;}
            const IPAddress src=udp.remoteIP();
            if(packet_size>static_cast<int>(reply.size())) {
                udp.flush();
                continue;
            }
            const int received=udp.read(reply.data(),reply.size());
            if(received!=packet_size)continue;
            // Reject responses from off-LAN hosts even if they resemble
            // Midea: this is a local setup shortlist only.
            bool on_subnet=true;
            for(uint8_t n=0;n<4;n++)
                on_subnet=on_subnet &&
                    (static_cast<uint8_t>(src[n]&mask[n])==
                     static_cast<uint8_t>(ip[n]&mask[n]));
            if(!on_subnet || src==ip)continue;
            enevina::midea::DiscoveredDevice candidate{};
            if(!enevina::midea::parse_discovery_reply(
                    reply.data(),static_cast<size_t>(received),candidate))
                continue;
            // Optional validated metadata identifies the appliance family.
            // Keep older/minimal discovery replies visible when it is absent.
            enevina::midea::DiscoveryInfo info{};
            const bool metadata_known=enevina::midea::parse_discovery_info(
                reply.data(),static_cast<size_t>(received),info);
            bool duplicate=false;
            for(size_t i=0;i<count;i++) {
                if(hits[i].id!=candidate.id || hits[i].protocol!=candidate.protocol)continue;
                duplicate=true;
                // A later complete reply can enrich the same host's shortlist
                // row; it cannot move an existing ID to a different address.
                if(metadata_known && hits[i].ip==src) {
                    hits[i].info=info;hits[i].metadata_known=true;
                }
            }
            if(duplicate)continue;
            if(count==hits.size()) {truncated=true;continue;}
            hits[count++]={src,candidate.id,candidate.protocol,metadata_known,info};
        }
        udp.stop();
        // Bounded and escaped-by-construction response; no Wi-Fi credential,
        // V3 token/key, MAC address, SSID, or sensor data is returned.
        char json[2048]{};
        size_t used=0;
        auto add=[&](const char* format,auto... args)->bool{
            if(used>=sizeof(json))return false;
            const int n=std::snprintf(json+used,sizeof(json)-used,format,args...);
            if(n<0 || static_cast<size_t>(n)>=sizeof(json)-used)return false;
            used+=static_cast<size_t>(n);
            return true;
        };
        bool ok=add("%s","{\"devices\":[");
        for(size_t i=0;i<count && ok;i++)
            ok=add("%s{\"ip\":\"%u.%u.%u.%u\",\"protocol\":%u,\"device_id\":\"%llu\","
                   "\"device_type_known\":%s,\"device_type\":%u,\"model_known\":%s,\"model\":\"%s\","
                   "\"subtype_known\":%s,\"subtype\":%u}",
                   i?",":"",hits[i].ip[0],hits[i].ip[1],
                   hits[i].ip[2],hits[i].ip[3],
                   static_cast<unsigned>(hits[i].protocol),
                   static_cast<unsigned long long>(hits[i].id),
                   hits[i].metadata_known?"true":"false",
                   static_cast<unsigned>(hits[i].info.device_type),
                   hits[i].metadata_known?"true":"false",hits[i].info.model.data(),
                   hits[i].metadata_known && hits[i].info.subtype_known?"true":"false",
                   static_cast<unsigned>(hits[i].info.subtype));
        if(ok)ok=add("],\"count\":%u,\"truncated\":%s}",
                      static_cast<unsigned>(count),truncated?"true":"false");
        web.sendHeader("Cache-Control","no-store");
        web.send(ok?200:500,"application/json",ok?json:"{\"error\":\"BUFFER\"}");
    });
    web.on("/api/midea/config",HTTP_POST,[&web](){
        // This is a development-only LAN enrollment endpoint. Reject open
        // setup-AP requests; secrets must not travel on the open Wi-Fi.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
               web.arg("token").c_str())) {
            web.send(403,"application/json","{\"saved\":false,\"error\":\"TOKEN\"}");
            return;
        }
        const String proto=web.arg("protocol");
        const uint8_t version=proto=="2"?2:proto=="3"?3:0;
        if(version==0) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"PROTOCOL\"}");
            return;
        }
        const String host=web.arg("host");
        const String id=web.arg("device_id");
        const String tok=web.arg("v3_token");
        const String key=web.arg("v3_key");
        if(host.length()>=sizeof(Config::host) ||
           id.length()>=sizeof(Config::device_id) ||
           tok.length()>=sizeof(Config::token_hex) ||
           key.length()>=sizeof(Config::key_hex) ||
           host.isEmpty() || id.isEmpty() ||
           (version==3 && (tok.isEmpty() || key.isEmpty()))) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"FIELDS\"}");
            return;
        }
        Config next{};
        next.protocol=version;
        std::memcpy(next.host,host.c_str(),host.length()+1);
        std::memcpy(next.device_id,id.c_str(),id.length()+1);
        // V2 has no per-device V3 tokens; do not retain them in the new
        // record when changing from V3 to V2.
        if(version==3) {
            std::memcpy(next.token_hex,tok.c_str(),tok.length()+1);
            std::memcpy(next.key_hex,key.c_str(),key.length()+1);
        }
        if(!valid_config(next)) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"INVALID\"}");
            return;
        }
        // Claim exclusive config transition BEFORE NVS I/O. No worker
        // can consume pending control, nor can HTTP queue a new command,
        // while these credentials are being committed.
        xSemaphoreTake(mutex,portMAX_DELAY);
        const bool writing=control_busy || config_updating;
        if(!writing) {
            config_updating=true;
            control_pending.store(false);
            pending_wifi_generation.store(0);
            pending_endpoint_generation.store(0);
            pilot={};
        }
        xSemaphoreGive(mutex);
        if(writing) {
            web.send(409,"application/json","{\"saved\":false,\"error\":\"CONTROL_BUSY\"}");
            return;
        }
        // Never print credentials or addresses to ESP logs or API responses.
        const bool committed=persist_config(next);
        xSemaphoreTake(mutex,portMAX_DELAY);
        config_updating=false;
        if(!committed) {
            xSemaphoreGive(mutex);
            web.send(507,"application/json","{\"saved\":false,\"error\":\"NVS\"}");
            return;
        }
        if(is_midea_selected())cottage_midea_automatic_cancel();
        saved=next;
        configured=true;
        pending_patch={};
        confirmed_command={};
        control_sent_utc.store(0);
        control_success.store(0);
        midea_mode_history.reset(millis());
        observed={};
        observed_wifi_generation=0;
        observed_endpoint_generation=0;
        last_success.store(0);
        last_result.store(0);
        revision.fetch_add(1);
        last_attempt.store(0);
        control_pending.store(false);
        control_result.store(-1);
        control_queued_utc.store(0);
        xSemaphoreGive(mutex);
        if(auto task=poll_task.load())xTaskNotifyGive(task);
        web.send(200,"application/json","{\"saved\":true,\"control_armed\":false}");
    });
    web.on("/api/midea/pilot",HTTP_POST,[&web](){
        // Volatile, explicit approval of this exact configured Midea device
        // and tester-verified temperature limits. Same existing settings
        // token, no new login/PIN; never available on the open setup AP.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
              web.arg("token").c_str())) {
            web.send(403,"application/json","{\"approved\":false,\"error\":\"TOKEN\"}");
            return;
        }
        const bool approve=web.arg("approve")=="true";
        const bool revoke=web.arg("approve")=="false";
        if(!approve && !revoke) {
            web.send(400,"application/json","{\"approved\":false,\"error\":\"APPROVAL\"}");
            return;
        }
        const String model=web.arg("model");
        double min_c=0,max_c=0;
        if(approve && (model.length()<3 || model.length()>=48 ||
           !web.hasArg("device_id") ||
           !strict_temperature(web.arg("min_c"),min_c) ||
           !strict_temperature(web.arg("max_c"),max_c) ||
           min_c<12 || max_c>40 || min_c>=max_c ||
           std::fabs(min_c*2-std::round(min_c*2))>0.00001 ||
           std::fabs(max_c*2-std::round(max_c*2))>0.00001)) {
            web.send(400,"application/json","{\"approved\":false,\"error\":\"MODEL_LIMITS\"}");
            return;
        }
        xSemaphoreTake(mutex,portMAX_DELAY);
        const bool busy=control_busy || config_updating ||
                        control_pending.load();
        const bool same= configured && is_midea_selected() &&
            (!approve || web.arg("device_id")==String(saved.device_id));
        if(!busy && same) {
            cottage_midea_automatic_cancel();
            pilot={};
            if(approve) {
                std::memcpy(pilot.model,model.c_str(),model.length()+1);
                std::memcpy(pilot.device_id,saved.device_id,
                            sizeof(pilot.device_id));
                pilot.protocol=saved.protocol;
                pilot.config_revision=revision.load();
                pilot.endpoint_generation=site_settings_endpoint_generation();
                pilot.min_c=min_c;pilot.max_c=max_c;
                pilot.enabled=true;
            }
        }
        xSemaphoreGive(mutex);
        if(busy || !same) {
            web.send(409,"application/json","{\"approved\":false,\"error\":\"BUSY_OR_DEVICE\"}");
            return;
        }
        web.send(200,"application/json",
                 approve?"{\"approved\":true}":"{\"approved\":false}");
    });
    web.on("/api/midea/control",HTTP_POST,[&web](){
        // Manual control uses the existing local settings authorization.
        // Never permit commands from the intentionally open setup-AP.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
              web.arg("token").c_str())) {
            web.send(403,"application/json","{\"queued\":false,\"error\":\"TOKEN\"}");
            return;
        }
        enevina::midea::ControlPatch patch{};
        uint8_t byte=0;
        if(web.hasArg("power")) {
            const String value=web.arg("power");
            if(value!="true" && value!="false") {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"POWER\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlPower;
            patch.power=value=="true";
        }
        if(web.hasArg("mode")) {
            if(!decimal_u8(web.arg("mode"),byte)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"MODE\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlMode;
            patch.mode=byte;
        }
        if(web.hasArg("target_c")) {
            if(!strict_temperature(web.arg("target_c"),patch.setpoint_c)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"TEMPERATURE\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlTemperature;
        }
        if(web.hasArg("fan")) {
            if(!decimal_u8(web.arg("fan"),byte)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"FAN\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlFan;
            patch.fan=byte;
        }
        if(web.hasArg("swing")) {
            if(!decimal_u8(web.arg("swing"),byte)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"SWING\"}");
                return;
            }
            patch.changed|=enevina::midea::ControlSwing;
            patch.swing=byte;
        }
        for(const auto& field:boolean_controls) {
            if(!web.hasArg(field.name))continue;
            const String value=web.arg(field.name);
            if(value!="true" && value!="false") {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"BOOLEAN\"}");
                return;
            }
            patch.changed|=field.field;
            patch.*(field.patch)=value=="true";
        }
        for(const auto& field:numeric_controls) {
            if(!web.hasArg(field.name))continue;
            if(!decimal_u8(web.arg(field.name),byte)) {
                web.send(400,"application/json","{\"queued\":false,\"error\":\"PROPERTY\"}");
                return;
            }
            patch.changed|=field.field;
            patch.*(field.patch)=byte;
        }
        if((patch.changed&enevina::midea::AcGeneralFields) &&
           (patch.changed&enevina::midea::AcPropertyFields)) {
            web.send(400,"application/json","{\"queued\":false,\"error\":\"MIXED_COMMAND_FAMILIES\"}");
            return;
        }
        if(!control_patch_fields_valid(patch)) {
            web.send(400,"application/json","{\"queued\":false,\"error\":\"INVALID\"}");
            return;
        }
        const auto now=static_cast<int64_t>(time(nullptr));
        bool ready=false;
        const char* request_error=nullptr;
        // The existing service owns one pump, one worker and one queued
        // command. No HTTP callback writes directly to the heat pump.
        xSemaphoreTake(mutex,portMAX_DELAY);
        const auto previous=last_success.load();
        const bool pilot_matches=pilot.enabled &&
              pilot.config_revision==revision.load() &&
              pilot.endpoint_generation==site_settings_endpoint_generation() &&
              pilot.protocol==saved.protocol &&
              !std::strcmp(pilot.device_id,saved.device_id);
        const double requested=(patch.changed&enevina::midea::ControlTemperature)?
              patch.setpoint_c:observed.setpoint_c;
        ready=configured && pilot_matches && !config_updating &&
              !cottage_midea_automatic_enabled() && !control_busy && !control_pending.load() &&
              std::isfinite(requested) &&
              requested>=pilot.min_c && requested<=pilot.max_c &&
              previous>0 && now>=previous && now-previous<=90 &&
              observed_wifi_generation==cottage_wifi_link_generation() &&
              observed_endpoint_generation==site_settings_endpoint_generation() &&
              poll_task.load()!=nullptr;
        if(ready && is_midea_selected()) {
            // Feature support is not sufficient for a safe whole-state write.
            // Validate this exact draft against the current observed profile
            // without sending or changing the pump; the worker checks again.
            request_error=control_request_error(observed,patch);
            if(!request_error) {
                queue_locked(patch,now,cottage_wifi_link_generation(),
                             site_settings_endpoint_generation(),nullptr);
            } else ready=false;
        } else ready=false;
        xSemaphoreGive(mutex);
        if(request_error) {
            char error[112]{};
            std::snprintf(error,sizeof(error),"{\"queued\":false,\"error\":\"%s\"}",request_error);
            web.send(400,"application/json",error);
            return;
        }
        if(!ready) {
            web.send(409,"application/json","{\"queued\":false,\"error\":\"NOT_READY\"}");
            return;
        }
        xTaskNotifyGive(poll_task.load());
        web.send(202,"application/json","{\"queued\":true}");
    });
    web.on("/api/midea/test",HTTP_POST,[&web](){
        // A manual diagnostic only schedules the same read-only worker.
        // Never send manufacturer control commands from this endpoint.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !enevina::cottage::c23_authorize_local_setting(
               web.arg("token").c_str())) {
            web.send(403,"application/json","{\"queued\":false,\"error\":\"TOKEN\"}");
            return;
        }
        bool ready=false;
        xSemaphoreTake(mutex,portMAX_DELAY);
        ready=configured;
        xSemaphoreGive(mutex);
        if(!is_midea_selected() || !ready || !poll_task.load()) {
            web.send(409,"application/json","{\"queued\":false,\"error\":\"NOT_READY\"}");
            return;
        }
        xTaskNotifyGive(poll_task.load());
        web.send(202,"application/json","{\"queued\":true}");
    });
}
bool cottage_midea_service_needs_worker() {
    if(!is_midea_selected() || !mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    const bool ready=configured;
    xSemaphoreGive(mutex);
    return ready;
}
void cottage_midea_service_worker(void*) {
    poll_task.store(xTaskGetCurrentTaskHandle());
    for(;;) {
        const uint32_t read_link=cottage_wifi_link_generation();
        const uint32_t read_endpoint=site_settings_endpoint_generation();
        const bool connected=WiFi.status()==WL_CONNECTED &&
                             !cottage_ota_network_maintenance();
        const bool selected=is_midea_selected();
        Config cfg{};
        bool ready=false,action=false;
        uint32_t before=0;
        uint32_t expected_link=0;
        uint32_t expected_endpoint=0;
        double min_c=0,max_c=0;
        bool approved=false;
        bool automatic=false;
        AutomaticWriteContext automatic_context{};
        enevina::midea::WriteReceipt receipt{};
        enevina::midea::ControlPatch patch{};
        xSemaphoreTake(mutex,portMAX_DELAY);
        cfg=saved;
        ready=configured && !config_updating.load();
        before=revision.load();
        if(control_pending.load()) {
            patch=pending_patch;
            automatic=pending_is_automatic;
            automatic_context=pending_automatic;
            expected_link=pending_wifi_generation.load();
            expected_endpoint=pending_endpoint_generation.load();
            min_c=pilot.min_c;max_c=pilot.max_c;
            approved=pilot.enabled &&
                     pilot.config_revision==before &&
                     pilot.endpoint_generation==expected_endpoint &&
                     expected_endpoint==site_settings_endpoint_generation() &&
                     pilot.protocol==cfg.protocol &&
                     !std::strcmp(pilot.device_id,cfg.device_id);
            control_pending.store(false);
            if(approved && !config_updating &&
               expected_link==cottage_wifi_link_generation() &&
               expected_endpoint==site_settings_endpoint_generation()) {
                control_busy=true;
                action=true;
            } else control_result.store(
                static_cast<int>(enevina::midea::ReadStatus::ResourceBusy));
        }
        xSemaphoreGive(mutex);
        enevina::midea::ReadStatus result=
            enevina::midea::ReadStatus::ResourceBusy;
        enevina::midea::State sample{};
        bool attempted=false;
        bool deferred_for_budget=false;
        // A momentary Wi-Fi disconnection must NEVER lead to a delayed
        // command firing later when the connection returns.
        const auto now=static_cast<int64_t>(time(nullptr));
        const auto queued=control_queued_utc.load();
        const auto* automatic_guard=automatic?&automatic_context:nullptr;
        const bool within_window=!action ||
            (queued>0 && now>=queued && now-queued<=15);
        if(connected && selected && ready && !config_updating &&
           valid_config(cfg) && within_window &&
           (!action || (approved &&
             expected_link==cottage_wifi_link_generation() &&
             expected_endpoint==site_settings_endpoint_generation()))) {
            // Independent worker, shared RAM budget, no HTTP callback writes.
            // A user-submitted command must not silently vanish because
            // BLE/weather TLS held the shared mutex for 300 ms. Give it a
            // short turn, then retry only while the SAME requested action
            // remains fresh, connected, and explicitly approved.
            enevina_firmware::NetworkBudgetLease lease(
                action?2500u:300u,60*1024,16*1024);
            if(!lease.held() && action)deferred_for_budget=true;
            const int64_t after_lease=static_cast<int64_t>(time(nullptr));
            if(lease.held() && (!action ||
                (queued>0 && after_lease>=queued &&
                 after_lease-queued<15))) {
                attempted=true;
                polling.store(true);
                if(!action)last_attempt.store(time(nullptr));
                else control_attempt.store(time(nullptr));
                if(cfg.protocol==2) {
                    uint64_t id=0;
                    if(enevina::midea::decode_device_id(cfg.device_id,id)) {
                        result=action?
                            enevina::midea::write_device_v2(
                                cfg.host,id,patch,sample,
                                expected_link,expected_endpoint,before,queued+15,min_c,max_c,
                                automatic_guard,&receipt):
                            enevina::midea::read_device_v2(cfg.host,id,sample,before);
                    } else result=enevina::midea::ReadStatus::InvalidCredentials;
                } else {
                    enevina::midea::Credential auth{};
                    if(enevina::midea::decode_credentials(
                       cfg.device_id,cfg.token_hex,cfg.key_hex,auth)) {
                        result=action?
                            enevina::midea::write_device(
                                cfg.host,auth,patch,sample,
                                expected_link,expected_endpoint,before,queued+15,min_c,max_c,
                                automatic_guard,&receipt):
                            enevina::midea::read_device(cfg.host,auth,sample,before);
                    } else result=enevina::midea::ReadStatus::InvalidCredentials;
                }
                polling.store(false);
            }
        }
        const bool repeat_allowed=deferred_for_budget &&
            (!automatic || cottage_midea_automatic_write_current(
                automatic_context.authorization_generation,
                automatic_context.control_config_generation,automatic_context.site_generation));
        xSemaphoreTake(mutex,portMAX_DELAY);
        const bool same_identity=before==revision.load() &&
            is_midea_selected() && WiFi.status()==WL_CONNECTED &&
            (action?expected_link:read_link)==cottage_wifi_link_generation() &&
            (action?expected_endpoint:read_endpoint)==site_settings_endpoint_generation();
        if(action) {
            if(same_identity && receipt.sent)control_sent_utc.store(time(nullptr));
            if(same_identity && receipt.attempted && (patch.changed&enevina::midea::ControlPower))
                midea_mode_history.command_dispatched(millis());
            // Resource admission failures happen BEFORE any device write.
            // Requeue the same command for a bounded retry; do not invent
            // a new authorization, reset its 15 s TTL, or retry a write
            // whose result is uncertain.
            const int64_t at=static_cast<int64_t>(time(nullptr));
            const int64_t issued=control_queued_utc.load();
            const bool repeat=repeat_allowed &&
                before==revision.load() && !config_updating &&
                pilot.enabled && pilot.config_revision==before &&
                pilot.endpoint_generation==expected_endpoint &&
                pilot.protocol==cfg.protocol &&
                !std::strcmp(pilot.device_id,cfg.device_id) &&
                is_midea_selected() && WiFi.status()==WL_CONNECTED &&
                !cottage_ota_network_maintenance() &&
                expected_link==cottage_wifi_link_generation() &&
                expected_endpoint==site_settings_endpoint_generation() &&
                issued>0 && at>=issued && at-issued<14;
            if(repeat) {
                pending_patch=patch;
                control_pending.store(true);
                // Preserve original queued UTC and WiFi generation.
            }
            if(result==enevina::midea::ReadStatus::Ok && !same_identity)
                result=enevina::midea::ReadStatus::ControlNotConfirmed;
            control_result.store(static_cast<int>(result));
            if(result==enevina::midea::ReadStatus::Ok && same_identity) {
                control_success.store(time(nullptr));
                confirmed_command={true,sample.power,sample.mode,sample.setpoint_c};
            }
            control_busy=false;
            deferred_for_budget=repeat;
        }
        if(connected && selected && ready && within_window && same_identity) {
            if(action || attempted) {
                last_result.store(static_cast<int>(result));
                if(result==enevina::midea::ReadStatus::Ok) {
                    const auto sample_link=action?expected_link:read_link;
                    const auto sample_endpoint=action?expected_endpoint:read_endpoint;
                    const int64_t sampled_utc=time(nullptr);
                    const uint32_t sampled_at_ms=millis();
                    const auto mode=!sample.power?enevina::PumpRoomMode::Off:
                        sample.mode==4?enevina::PumpRoomMode::Heat:enevina::PumpRoomMode::Other;
                    observed_request.observe(mode,sample.setpoint_c,
                        static_cast<uint64_t>(esp_timer_get_time()/1000),
                        fresh_locked(sampled_utc,sample_link,sample_endpoint));
                    if(action && automatic && receipt.sent) {
                        automatic_confirmation_generation=observed_request.generation();
                        automatic_queued_utc=control_queued_utc.load();
                        automatic_confirmed_utc=sampled_utc;
                    }
                    observed=sample;
                    if(observed_wifi_generation!=sample_link ||
                       observed_endpoint_generation!=sample_endpoint)
                        midea_mode_history.reset(sampled_at_ms);
                    if(!sample.power || sample.mode==4)
                        midea_mode_history.observe(sample.power,sampled_at_ms,true);
                    else midea_mode_history.reset(sampled_at_ms);
                    observed_wifi_generation=sample_link;
                    observed_endpoint_generation=sample_endpoint;
                    last_success.store(time(nullptr));
                } else last_success.store(0);
            }
        } else last_success.store(0);
        xSemaphoreGive(mutex);
        // A user action or settings change wakes the worker immediately.
        // Normal read polling remains every 30 seconds.
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(
            deferred_for_budget?500u:30000u));
    }
}

#endif
