#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <cJSON.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include "cottage_config.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_air_service.hpp"
#include "pump/adapter_catalog.hpp"

using enevina::cottage::SiteSettings;
using enevina::cottage::SettingError;
namespace {
constexpr uint32_t kMagic=0x45535431; // "EST1"
constexpr uint16_t kVersion=1;
struct Stored {
    uint32_t magic=kMagic;
    uint16_t version=kVersion;
    uint16_t size=sizeof(SiteSettings);
    SiteSettings settings{};
};
SemaphoreHandle_t guard=nullptr;
SiteSettings current{};
std::atomic<uint32_t> generation{0};
std::atomic<uint32_t> endpoint_generation{0};
bool loaded_from_nvs=false;

SiteSettings compiled_defaults() {
    // Existing compile-time pilot values remain the defaults. Copy only
    // values that pass the same validation as browser input.
    SiteSettings s{};
    s.tariff.margin_eur_per_kwh=COTTAGE_SPOT_MARGIN;
    s.tariff.transfer_day_eur_per_kwh=COTTAGE_TRANSFER_DAY;
    s.tariff.transfer_night_eur_per_kwh=COTTAGE_TRANSFER_NIGHT;
    s.tariff.electricity_tax_eur_per_kwh=COTTAGE_ELECTRICITY_TAX;
    s.tariff.vat_rate=COTTAGE_VAT_RATE;
    s.tariff.kind=enevina::cottage::TransferKind::TimeOfDay;
    if(!enevina::cottage::valid_site_tariff(s.tariff)) s.tariff=enevina::cottage::SiteTariff{};
    using enevina::cottage::apply_site_setting;
    if(apply_site_setting(s,"cn105_url",COTTAGE_CN105_URL)!=SettingError::Ok) s.cn105_url[0]=0;
    if(apply_site_setting(s,"shelly_url",COTTAGE_SHELLY_URL)!=SettingError::Ok) s.shelly_url[0]=0;
    if(apply_site_setting(s,"fmi_latlon",COTTAGE_FMI_LATLON)!=SettingError::Ok) s.fmi_latlon[0]=0;
    if(strlen(COTTAGE_CN105_URL))
        strlcpy(s.pump_adapter,"mitsubishi_cn105_esphome",sizeof(s.pump_adapter));
    return s;
}
bool acceptable(const SiteSettings& s) {
    return enevina::cottage::valid_site_tariff(s.tariff) &&
        std::isfinite(s.min_price_advantage_fraction) &&
        s.min_price_advantage_fraction>=0 && s.min_price_advantage_fraction<=0.5 &&
        (!s.pump_adapter[0] || enevina::adapter_selectable_in_lite_firmware(s.pump_adapter)) &&
        memchr(s.cn105_url,0,sizeof(s.cn105_url)) &&
        memchr(s.shelly_url,0,sizeof(s.shelly_url)) &&
        memchr(s.fmi_latlon,0,sizeof(s.fmi_latlon)) &&
        memchr(s.pump_adapter,0,sizeof(s.pump_adapter));
}
bool persist(const SiteSettings& s) {
    Stored blob{};
    blob.settings=s;
    Preferences p;
    if(!p.begin("site",false)) return false;
    const size_t written=p.putBytes("v1",&blob,sizeof(blob));
    p.end();
    return written==sizeof(blob);
}
const char* kind_name(enevina::cottage::TransferKind k) {
    switch(k) {
    case enevina::cottage::TransferKind::General:return "general";
    case enevina::cottage::TransferKind::TimeOfDay:return "time_of_day";
    case enevina::cottage::TransferKind::Seasonal:return "seasonal";
    }
    return "time_of_day";
}
void send_settings(WebServer& web) {
    const SiteSettings s=site_settings_snapshot();
    cJSON* root=cJSON_CreateObject();
    if(!root) {web.send(503,"application/json","{}");return;}
    cJSON_AddBoolToObject(root,"stored_in_device",loaded_from_nvs);
    cJSON_AddNumberToObject(root,"generation",generation.load());
    cJSON* t=cJSON_AddObjectToObject(root,"tariff");
    cJSON_AddNumberToObject(t,"margin_c_per_kwh",s.tariff.margin_eur_per_kwh*100);
    cJSON_AddNumberToObject(t,"transfer_day_c_per_kwh",s.tariff.transfer_day_eur_per_kwh*100);
    cJSON_AddNumberToObject(t,"transfer_night_c_per_kwh",s.tariff.transfer_night_eur_per_kwh*100);
    cJSON_AddNumberToObject(t,"electricity_tax_c_per_kwh",s.tariff.electricity_tax_eur_per_kwh*100);
    cJSON_AddNumberToObject(t,"vat_percent",s.tariff.vat_rate*100);
    cJSON_AddBoolToObject(t,"components_include_vat",s.tariff.components_include_vat);
    cJSON_AddStringToObject(t,"transfer_kind",kind_name(s.tariff.kind));
    cJSON_AddNumberToObject(t,"day_start_hour",s.tariff.day_start_hour);
    cJSON_AddNumberToObject(t,"night_start_hour",s.tariff.night_start_hour);
    cJSON_AddNumberToObject(root,"min_price_advantage_percent",s.min_price_advantage_fraction*100);
    cJSON_AddStringToObject(root,"cn105_url",s.cn105_url);
    cJSON_AddStringToObject(root,"shelly_url",s.shelly_url);
    cJSON_AddStringToObject(root,"fmi_latlon",s.fmi_latlon);
    cJSON_AddStringToObject(root,"fmi_latlon_note","takes effect after next power-up");
    cJSON_AddStringToObject(root,"pump_adapter",s.pump_adapter);
    cJSON* list=cJSON_AddArrayToObject(root,"adapters");
    for(const auto& e:enevina::adapter_catalog) {
        cJSON* a=cJSON_CreateObject();
        if(!a) break;
        cJSON_AddStringToObject(a,"id",e.id);
        cJSON_AddStringToObject(a,"manufacturer",e.manufacturer);
        cJSON_AddStringToObject(a,"models",e.models);
        cJSON_AddStringToObject(a,"link",enevina::adapter_link_name(e.link));
        cJSON_AddStringToObject(a,"maturity",enevina::adapter_maturity_name(e.maturity));
        cJSON_AddBoolToObject(a,"selectable",enevina::adapter_selectable_in_lite_firmware(e.id));
        cJSON_AddStringToObject(a,"measurements",e.measurements);
        cJSON_AddStringToObject(a,"commands",e.commands);
        if(e.setpoint_max_c>0) {
            cJSON_AddNumberToObject(a,"setpoint_min_c",e.setpoint_min_c);
            cJSON_AddNumberToObject(a,"setpoint_max_c",e.setpoint_max_c);
            cJSON_AddNumberToObject(a,"setpoint_step_c",e.setpoint_step_c);
        }
        cJSON_AddStringToObject(a,"freshness",e.freshness);
        cJSON_AddBoolToObject(a,"command_readback",e.command_readback);
        cJSON_AddItemToArray(list,a);
    }
    char* body=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!body) {web.send(503,"application/json","{}");return;}
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8",body);
    cJSON_free(body);
}
} // namespace

void site_settings_initialize() {
    guard=xSemaphoreCreateMutex();
    if(!guard) abort();
    SiteSettings s=compiled_defaults();
    Preferences p;
    if(p.begin("site",true)) {
        Stored blob{};
        if(p.getBytesLength("v1")==sizeof(blob) &&
           p.getBytes("v1",&blob,sizeof(blob))==sizeof(blob) &&
           blob.magic==kMagic && blob.version==kVersion &&
           blob.size==sizeof(SiteSettings) && acceptable(blob.settings)) {
            s=blob.settings;
            loaded_from_nvs=true;
        }
        p.end();
    }
    current=s;
}
SiteSettings site_settings_snapshot() {
    if(!guard) return SiteSettings{};
    xSemaphoreTake(guard,portMAX_DELAY);
    const SiteSettings copy=current;
    xSemaphoreGive(guard);
    return copy;
}
uint32_t site_settings_generation() { return generation.load(); }
uint32_t site_settings_endpoint_generation() { return endpoint_generation.load(); }
bool site_slot_total_price(int64_t utc,double spot,double& total) {
    const SiteSettings s=site_settings_snapshot();
    return enevina::cottage::site_total_price(s.tariff,utc,spot,total);
}
void site_settings_register_routes(WebServer& web) {
    web.on("/api/settings",HTTP_GET,[&web]() {send_settings(web);});
    web.on("/api/settings",HTTP_POST,[&web]() {
        // Same existing per-boot local CSRF token as the other dashboard
        // settings; no new password, login or security gate.
        if(!web.hasArg("token") || !enevina::cottage::c23_authorize_local_setting(web.arg("token").c_str())) {
            web.send(403,"application/json","{\"saved\":false,\"error\":\"TOKEN\"}");
            return;
        }
        SiteSettings next=site_settings_snapshot();
        for(int i=0;i<web.args();++i) {
            const String key=web.argName(i);
            if(key=="token" || key=="plain") continue;
            const auto err=enevina::cottage::apply_site_setting(
                next,key.c_str(),web.arg(i).c_str());
            if(err!=SettingError::Ok) {
                String body="{\"saved\":false,\"error\":\"";
                body+=enevina::cottage::setting_error_name(err);
                body+="\",\"key\":\"";
                // Echo only a known-safe key name (letters/underscore).
                for(char c:key) if((c>='a'&&c<='z')||c=='_') body+=c;
                body+="\"}";
                web.send(400,"application/json",body);
                return;
            }
        }
        if(next.pump_adapter[0] &&
           !enevina::adapter_selectable_in_lite_firmware(next.pump_adapter)) {
            web.send(400,"application/json",
                "{\"saved\":false,\"error\":\"ADAPTER_NOT_SUPPORTED\",\"key\":\"pump_adapter\"}");
            return;
        }
        if(!acceptable(next)) {
            web.send(400,"application/json","{\"saved\":false,\"error\":\"INVALID\"}");
            return;
        }
        const SiteSettings previous=site_settings_snapshot();
        const bool endpoint_changed=
            strcmp(previous.cn105_url,next.cn105_url)!=0 ||
            strcmp(previous.pump_adapter,next.pump_adapter)!=0;
        // Durable revocation MUST precede new site commit. Otherwise an
        // immediate reset could reinstate the old pump permission when the
        // user later selects the previous URL again.
        if(endpoint_changed && !cottage_control_revoke_saved_intent()) {
            web.send(500,"application/json",
                     "{\"saved\":false,\"error\":\"CONTROL_NVS\"}");
            return;
        }
        // Durable first: RAM changes only after NVS accepted the full blob.
        if(!persist(next)) {
            web.send(500,"application/json","{\"saved\":false,\"error\":\"NVS\"}");
            return;
        }
        xSemaphoreTake(guard,portMAX_DELAY);
        // The previous endpoint's NVS authorization was already revoked.
        current=next;
        loaded_from_nvs=true;
        xSemaphoreGive(guard);
        if(endpoint_changed) endpoint_generation.fetch_add(1);
        generation.fetch_add(1);
        web.send(200,"application/json",endpoint_changed?
            "{\"saved\":true,\"control_disarmed\":true}":"{\"saved\":true}");
    });
}
#endif // COTTAGE_BENCH_MODE
