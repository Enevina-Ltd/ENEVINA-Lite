#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <Preferences.h>
#include <nvs.h>
#include <esp_log.h>
#include "cottage/nvs_cache_reclaim_policy.hpp"
#include <WebServer.h>
#include <cJSON.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include "cottage_config.hpp"
#include "cottage_site_settings.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage_air_service.hpp"
#include "../../main/weather.hpp"
#include "cottage_wifi.hpp"
#include "pump/adapter_catalog.hpp"
#include "../../main/market.hpp"
#include "forecast/market_zones.hpp"
#include "cottage_geocode.hpp"

using enevina::cottage::SiteSettings;
using enevina::cottage::SettingError;
namespace {
constexpr uint32_t kMagic=0x45535431; // "EST1"
constexpr uint16_t kVersion=4;  // v4 never overwrites rollback v2/v3
struct Stored {
    uint32_t magic=kMagic;
    uint16_t version=kVersion;
    uint16_t size=sizeof(SiteSettings);
    SiteSettings settings{};
    uint64_t rollback_v2_signature=0;
    uint64_t rollback_v3_signature=0; // both snapshots travel atomically with v4
};
// v3 blobs are read as versioned prefixes, so both the older pre-signature
// layout and the signature-bearing v3 draft remain upgrade-readable.
using enevina::cottage::SiteSettingsV1;
struct StoredV1 {
    uint32_t magic=kMagic;
    uint16_t version=1;
    uint16_t size=sizeof(SiteSettingsV1);
    SiteSettingsV1 settings{};
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
    // A factory-fresh device runs the first-start wizard. A pilot build that
    // was compiled with a pump address is already configured by its owner.
    s.setup_complete=strlen(COTTAGE_CN105_URL)>0;
    s.setup_step=0;
    return s;
}
bool acceptable(const SiteSettings& s) {
    return enevina::cottage::valid_site_settings(s) &&
        std::isfinite(s.min_price_advantage_fraction) &&
        s.min_price_advantage_fraction>=0 && s.min_price_advantage_fraction<=0.5 &&
        (!s.pump_adapter[0] || enevina::adapter_selectable_in_lite_firmware(s.pump_adapter)) &&
        memchr(s.cn105_url,0,sizeof(s.cn105_url)) &&
        memchr(s.shelly_url,0,sizeof(s.shelly_url)) &&
        memchr(s.fmi_latlon,0,sizeof(s.fmi_latlon)) &&
        memchr(s.pump_adapter,0,sizeof(s.pump_adapter));
}
// Only these two NVS values are reconstructible from remote data.
// Never touch the owner's Wi-Fi, OTA journal, Ruuvi history or authorization.
bool erase_disposable_cache(const char* nspace,const char* key) {
    nvs_handle_t handle=0;
    if(nvs_open(nspace,NVS_READWRITE,&handle)!=ESP_OK)return false;
    const esp_err_t erased=nvs_erase_key(handle,key);
    const bool ok=erased==ESP_OK && nvs_commit(handle)==ESP_OK;
    nvs_close(handle);
    if(ok) ESP_LOGW("enevina_site","NVS cache reclaimed: %s/%s",nspace,key);
    return ok;
}
// Snapshot rollback-readable settings records before writing v4. Their
// signatures travel INSIDE the same v4 blob: no split-metadata NVS commit.
uint64_t rollback_key_signature(Preferences& p,const char* key) {
    const size_t n=p.getBytesLength(key);
    if(n==0 || n>sizeof(Stored))return 0;
    uint8_t raw[sizeof(Stored)]{};
    if(p.getBytes(key,raw,n)!=n)return 0;
    return enevina::cottage::site_v2_signature(raw,n);
}
bool persist(const SiteSettings& s) {
    Stored blob{};
    blob.settings=s;
    const auto write_site=[&]() {
        Preferences p;
        if(!p.begin("site",false)) return false;
        // Preserve both old versions during OTA probation. Rollback to v2
        // or v3 can edit its own key; the next v4 boot detects the change.
        blob.rollback_v2_signature=rollback_key_signature(p,"v2");
        blob.rollback_v3_signature=rollback_key_signature(p,"v3");
        const size_t written=p.putBytes("v4",&blob,sizeof(blob));
        if(written==sizeof(blob) && !cottage_ota_confirmation_pending()) {
            // Reclamation is allowed only AFTER OTA confirmation, when the
            // earlier firmware cannot be restored by the OTA rollback path.
            if(p.isKey("v3")) p.remove("v3");
            if(p.isKey("v2")) p.remove("v2");
            if(p.isKey("v1")) p.remove("v1");
        }
        p.end();
        return written==sizeof(blob);
    };
    // NVS in existing installed devices is just 16KiB. With persistent
    // weather, price, Ruuvi history and Wi-Fi, setting a site identity may
    // fail even when the NVS namespace exists. Reclaim only the two online
    // caches when the *user-initiated durable write* has failed. Don't touch
    // non-cache NVS keys or clear a whole partition.
    return enevina::cottage::persist_with_optional_cache_reclaim(
        write_site,erase_disposable_cache);
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
    cJSON_AddStringToObject(root,"fmi_latlon_note","applied immediately after save");
    cJSON_AddStringToObject(root,"pump_adapter",s.pump_adapter);
    cJSON* optimization=cJSON_AddObjectToObject(root,"optimization");
    if(optimization) {
        cJSON_AddStringToObject(optimization,"profile",enevina::cottage::profile_name(s.profile));
        if(s.comfort_min_c>0) cJSON_AddNumberToObject(optimization,"comfort_min_c",s.comfort_min_c);
        else cJSON_AddNullToObject(optimization,"comfort_min_c");
        if(s.comfort_max_c>0) cJSON_AddNumberToObject(optimization,"comfort_max_c",s.comfort_max_c);
        else cJSON_AddNullToObject(optimization,"comfort_max_c");
    }
    cJSON* region=cJSON_AddObjectToObject(root,"region");
    if(region) {
        cJSON_AddStringToObject(region,"country",s.country);
        cJSON_AddStringToObject(region,"price_zone",s.price_zone);
        cJSON_AddStringToObject(region,"city",s.city);
        cJSON_AddStringToObject(region,"timezone",s.timezone);
        cJSON_AddStringToObject(region,"language",s.language);
        cJSON_AddBoolToObject(region,"tariff_confirmed",s.tariff_confirmed);
        cJSON_AddBoolToObject(region,"setup_complete",s.setup_complete);
        cJSON_AddNumberToObject(region,"setup_step",s.setup_step);
        const char* area=nullptr;const char* currency=nullptr;int offset=0;
        const bool resolved=enevina::cottage::site_market_region(s,area,currency,offset);
        cJSON_AddBoolToObject(region,"price_region_resolved",resolved);
        if(resolved) {
            cJSON_AddStringToObject(region,"currency",currency);
            cJSON_AddStringToObject(region,"nordpool_area",area);
            cJSON_AddNumberToObject(region,"std_utc_offset_hours",offset);
        }
    }
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
void apply_market_region(const SiteSettings& s) {
    const char* area=nullptr;const char* currency=nullptr;int offset=0;
    if(enevina::cottage::site_market_region(s,area,currency,offset))
        enevina_firmware::market_set_region(area,currency);
    else
        enevina_firmware::market_set_region(nullptr,nullptr);   // not chosen yet
}
} // namespace

void site_settings_initialize() {
    guard=xSemaphoreCreateMutex();
    if(!guard) abort();
    SiteSettings s=compiled_defaults();
    Preferences p;
    if(p.begin("site",true)) {
        Stored blob{};
        StoredV1 old{};
        bool v4_valid=false;
        // v4 is the authoritative candidate unless rollback firmware changed
        // a protected v3 or v2 record since v4's last durable save.
        if(p.getBytesLength("v4")==sizeof(blob) &&
           p.getBytes("v4",&blob,sizeof(blob))==sizeof(blob) &&
           blob.magic==kMagic && blob.version==kVersion &&
           blob.size==sizeof(SiteSettings) && acceptable(blob.settings)) {
            s=blob.settings;
            loaded_from_nvs=true;
            v4_valid=true;
        }
        const size_t h=offsetof(Stored,settings);
        // v3 is a full settings record, optionally followed by a prior
        // v2 fingerprint. Decode only the validated settings prefix.
        uint8_t raw3[sizeof(Stored)]{};
        const size_t len3=p.getBytesLength("v3");
        SiteSettings from3{};
        bool v3_valid=false;
        if(len3>h && len3<=sizeof(raw3) &&
           p.getBytes("v3",raw3,len3)==len3) {
            uint32_t magic=0;uint16_t version=0,size=0;
            memcpy(&magic,raw3,4);memcpy(&version,raw3+4,2);
            memcpy(&size,raw3+6,2);
            v3_valid=magic==kMagic && version==3 && size>0 &&
                size<=len3-h &&
                enevina::cottage::restore_site_settings(raw3+h,size,from3) &&
                acceptable(from3);
        }
        // v2 used several supported prefix layouts as fields were appended.
        uint8_t raw2[sizeof(Stored)]{};
        const size_t len2=p.getBytesLength("v2");
        SiteSettings from2{};
        bool v2_valid=false;
        if(len2>h && len2<=sizeof(raw2) &&
           p.getBytes("v2",raw2,len2)==len2) {
            uint32_t magic=0;uint16_t version=0,size=0;
            memcpy(&magic,raw2,4);memcpy(&version,raw2+4,2);
            memcpy(&size,raw2+6,2);
            v2_valid=magic==kMagic && version==2 &&
                size<=len2-h &&
                enevina::cottage::restore_site_settings(raw2+h,size,from2) &&
                acceptable(from2);
        }
        if(v4_valid) {
            // Priority: the newest rollback-capable version whose record
            // changed since the v4 candidate saved. Invalid records never
            // replace a validated v4 record.
            if(v3_valid && enevina::cottage::site_rollback_v2_modified(
                    blob.rollback_v3_signature,raw3,len3)) {
                s=from3;
            } else if(v2_valid && enevina::cottage::site_rollback_v2_modified(
                    blob.rollback_v2_signature,raw2,len2)) {
                s=from2;
            }
        } else if(v3_valid) {
            s=from3;loaded_from_nvs=true;
        } else if(v2_valid) {
            s=from2;loaded_from_nvs=true;
        }
        if(!loaded_from_nvs && p.getBytesLength("v1")==sizeof(old) &&
           p.getBytes("v1",&old,sizeof(old))==sizeof(old) &&
           old.magic==kMagic && old.version==1 &&
           old.size==sizeof(SiteSettingsV1)) {
            const SiteSettings migrated=
                enevina::cottage::site_settings_from_v1(old.settings);
            if(acceptable(migrated)) {
                s=migrated;
                loaded_from_nvs=true;
            }
        }
        p.end();
    }
    current=s;
    apply_market_region(s);
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
    return enevina::cottage::site_region_total_price(s,utc,spot,total);
}
// Static, device-independent table of supported countries and bidding zones.
static void send_regions(WebServer& web) {
    cJSON* root=cJSON_CreateObject();
    cJSON* list=root ? cJSON_AddArrayToObject(root,"countries") : nullptr;
    if(!list) {cJSON_Delete(root);web.send(503,"application/json","{}");return;}
    for(const auto& c:enevina::market_countries) {
        cJSON* o=cJSON_CreateObject();
        if(!o) break;
        cJSON_AddStringToObject(o,"iso",c.iso);
        cJSON_AddStringToObject(o,"name_en",c.name_en);
        cJSON_AddStringToObject(o,"name_fi",c.name_fi);
        cJSON_AddStringToObject(o,"currency",c.currency);
        cJSON_AddNumberToObject(o,"std_utc_offset_hours",c.std_offset_hours);
        cJSON* zones=cJSON_AddArrayToObject(o,"zones");
        for(uint8_t i=0;zones && i<c.zone_count;++i) {
            cJSON* z=cJSON_CreateObject();
            if(!z) break;
            cJSON_AddStringToObject(z,"id",c.zones[i].id);
            cJSON_AddStringToObject(z,"name_en",c.zones[i].name_en);
            cJSON_AddStringToObject(z,"name_fi",c.zones[i].name_fi);
            cJSON_AddItemToArray(zones,z);
        }
        cJSON_AddItemToArray(list,o);
    }
    char* body=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!body) {web.send(503,"application/json","{}");return;}
    web.sendHeader("Cache-Control","public, max-age=3600");
    web.send(200,"application/json; charset=utf-8",body);
    cJSON_free(body);
}
// City search. The worker task does the HTTPS request; this handler only
// starts/polls the job, so the web loop is never blocked by the network.
static void send_geocode(WebServer& web) {
    using namespace enevina_firmware;
    const String q=web.arg("q");
    const String country=web.arg("country");
    const String lang=web.arg("lang");
    auto snap=std::unique_ptr<GeocodeSnapshot>(new(std::nothrow) GeocodeSnapshot);
    if(!snap) {web.send(503,"application/json","{\"state\":\"failed\",\"error\":\"LOW_MEMORY\"}");return;}
    const GeocodeState st=geocode_search(q.c_str(),country.c_str(),lang.c_str(),*snap);
    cJSON* root=cJSON_CreateObject();
    if(!root) {web.send(503,"application/json","{}");return;}
    static const char* const names[]={"idle","pending","done","failed"};
    static const char* const errors[]={"NONE","BAD_QUERY","BUSY","OFFLINE","SERVER_ERROR",
                                       "BAD_RESPONSE","LOW_MEMORY"};
    cJSON_AddStringToObject(root,"state",names[static_cast<int>(st)&3]);
    if(st==GeocodeState::Failed)
        cJSON_AddStringToObject(root,"error",errors[static_cast<int>(snap->error)%7]);
    cJSON* list=cJSON_AddArrayToObject(root,"results");
    for(size_t i=0;st==GeocodeState::Done && list && i<snap->count;++i) {
        const auto& p=snap->places[i];
        cJSON* o=cJSON_CreateObject();
        if(!o) break;
        char latlon[32];
        cJSON_AddStringToObject(o,"name",p.name);
        cJSON_AddStringToObject(o,"region",p.region);
        cJSON_AddStringToObject(o,"country",p.country_iso);
        cJSON_AddStringToObject(o,"country_name",p.country_name);
        cJSON_AddNumberToObject(o,"lat",p.lat);
        cJSON_AddNumberToObject(o,"lon",p.lon);
        if(enevina::geocode_latlon_text(p,latlon,sizeof(latlon)))
            cJSON_AddStringToObject(o,"latlon",latlon);
        cJSON_AddItemToArray(list,o);
    }
    char* body=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!body) {web.send(503,"application/json","{}");return;}
    web.sendHeader("Cache-Control","no-store");
    web.send(st==GeocodeState::Pending?202:200,"application/json; charset=utf-8",body);
    cJSON_free(body);
}
void site_settings_register_routes(WebServer& web) {
    web.on("/api/geocode",HTTP_GET,[&web]() {
        if(cottage_wifi_require_home_lan(web))send_geocode(web);
    });
    web.on("/api/regions",HTTP_GET,[&web]() {
        if(cottage_wifi_require_home_lan(web))send_regions(web);
    });
    web.on("/api/settings",HTTP_GET,[&web]() {
        if(cottage_wifi_require_home_lan(web))send_settings(web);
    });
    web.on("/api/settings",HTTP_POST,[&web]() {
        if(!cottage_wifi_require_home_lan(web))return;
        // Same existing per-boot local CSRF token as the other dashboard
        // settings; no new password, login or security gate.
        if(!web.hasArg("token") || !enevina::cottage::c23_authorize_local_setting(web.arg("token").c_str())) {
            web.send(403,"application/json","{\"saved\":false,\"error\":\"TOKEN\"}");
            return;
        }
        SiteSettings next=site_settings_snapshot();
        {
            // Copy the form once; region keys are applied first (see
            // apply_site_settings_batch) so field order never matters.
            constexpr int kMaxFields=40;
            String names[kMaxFields],vals[kMaxFields];
            const char* keys[kMaxFields];
            const char* values[kMaxFields];
            size_t n=0;
            if(web.args()>kMaxFields+2) {   // never silently drop fields
                web.send(400,"application/json","{\"saved\":false,\"error\":\"TOO_MANY_FIELDS\"}");
                return;
            }
            for(int i=0;i<web.args() && n<kMaxFields;++i) {
                names[n]=web.argName(i);
                if(names[n]=="token" || names[n]=="plain") continue;
                vals[n]=web.arg(i);
                keys[n]=names[n].c_str();
                values[n]=vals[n].c_str();
                ++n;
            }
            size_t bad=0;
            const auto err=enevina::cottage::apply_site_settings_batch(
                next,n,keys,values,bad);
            if(err!=SettingError::Ok) {
                String body="{\"saved\":false,\"error\":\"";
                body+=enevina::cottage::setting_error_name(err);
                body+="\",\"key\":\"";
                // Echo only a known-safe key name (letters/underscore).
                for(const char* k=keys[bad];*k;++k)
                    if((*k>='a'&&*k<='z')||*k=='_') body+=*k;
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
        // Only weather location changed: keep the CN105 control permission.
        // Persist before updating RAM; the FMI worker invalidates its old
        // cache and immediately schedules a fetch for this location.
        if(strcmp(previous.fmi_latlon,next.fmi_latlon)!=0)
            enevina_firmware::weather_update_location(next.fmi_latlon);
        // Only a changed zone/currency touches the price cache; any other
        // setting (tariff, comfort, sensors) leaves prices untouched.
        if(strcmp(previous.country,next.country)!=0 ||
           strcmp(previous.price_zone,next.price_zone)!=0)
            apply_market_region(next);
        web.send(200,"application/json",endpoint_changed?
            "{\"saved\":true,\"control_disarmed\":true}":"{\"saved\":true}");
    });
}
#endif // COTTAGE_BENCH_MODE
