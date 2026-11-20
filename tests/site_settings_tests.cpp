#include "cottage/site_settings.hpp"
#include "cottage/comfort_transaction.hpp"
#include "pump/adapter_catalog.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace enevina::cottage;
static bool near(double a,double b){return std::fabs(a-b)<1e-12;}

static void total_price_rules() {
    SiteTariff t{};
    t.margin_eur_per_kwh=0.004;t.transfer_day_eur_per_kwh=0.04;
    t.transfer_night_eur_per_kwh=0.02;t.electricity_tax_eur_per_kwh=0.0224;
    t.vat_rate=0.255;
    double total=0;
    // 2026-01-12 10:00 UTC = 12:00 EET Monday: day transfer, VAT on everything.
    assert(site_total_price(t,1768212000,0.10,total));
    assert(near(total,(0.10+0.004+0.04+0.0224)*1.255));
    // 21:00 UTC = 23:00 EET: night transfer.
    assert(site_total_price(t,1768251600,0.10,total));
    assert(near(total,(0.10+0.004+0.02+0.0224)*1.255));
    // Components quoted INCLUDING VAT: VAT added once, to spot only.
    t.components_include_vat=true;
    assert(site_total_price(t,1768212000,0.10,total));
    assert(near(total,0.10*1.255+0.004+0.04+0.0224));
    // Negative spot keeps its sign; fixed components are unchanged.
    assert(site_total_price(t,1768212000,-0.05,total));
    assert(near(total,-0.05*1.255+0.004+0.04+0.0224));
    // General transfer: night price never used.
    t.kind=TransferKind::General;
    assert(site_total_price(t,1768251600,0.10,total));
    assert(near(total,0.10*1.255+0.004+0.04+0.0224));
    assert(!site_total_price(t,1768212000,NAN,total));
}
static void seasonal_and_dst() {
    SiteTariff t{};
    t.kind=TransferKind::Seasonal;
    bool day=false;
    // Mon 12.1.2026 12:00 EET winter weekday -> winter-day price.
    assert(site_transfer_is_day(t,1768212000,day) && day);
    // Sun 11.1.2026 12:00 EET -> other time.
    assert(site_transfer_is_day(t,1768125600,day) && !day);
    // Mon 13.4.2026 12:00 EEST (09:00 UTC) -> summer, other time.
    assert(site_transfer_is_day(t,1776070800,day) && !day);
    // Time-of-day with custom hours 06-21; DST end night 25.10.2026.
    t.kind=TransferKind::TimeOfDay;t.day_start_hour=6;t.night_start_hour=21;
    assert(site_transfer_is_day(t,1792904400-3600,day) && day);   // 04:00 UTC = 06:00 EET
    assert(site_transfer_is_day(t,1792904400-3600-900,day) && !day);
    // 01:00 UTC 25.10.: first EET hour. 00:45 UTC is 03:45 EEST, 01:00 is 03:00 EET.
    int h=0;
    assert(finnish_local_hour(1792890000-900,h) && h==3);
    assert(finnish_local_hour(1792890000,h) && h==3);
    t.day_start_hour=t.night_start_hour=7;
    assert(!valid_site_tariff(t));
    // Overnight "day" window (e.g. 22-07 cheaper day as configured) wraps.
    t.day_start_hour=22;t.night_start_hour=7;
    assert(valid_site_tariff(t));
    assert(site_transfer_is_day(t,1768251600,day) && day);       // 23:00 EET
}
static void parsing() {
    SiteSettings s{};
    assert(apply_site_setting(s,"margin_c_per_kwh","0,49")==SettingError::Ok);
    assert(near(s.tariff.margin_eur_per_kwh,0.0049));
    assert(apply_site_setting(s,"transfer_day_c_per_kwh","3.45")==SettingError::Ok);
    assert(apply_site_setting(s,"transfer_day_c_per_kwh","abc")==SettingError::BadNumber);
    assert(apply_site_setting(s,"transfer_day_c_per_kwh","0x10")==SettingError::BadNumber);
    assert(apply_site_setting(s,"transfer_day_c_per_kwh","nan")==SettingError::BadNumber);
    assert(apply_site_setting(s,"transfer_day_c_per_kwh","-1")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"transfer_day_c_per_kwh","")==SettingError::BadNumber);
    assert(near(s.tariff.transfer_day_eur_per_kwh,0.0345)); // rejected input left it
    assert(apply_site_setting(s,"vat_percent","25,5")==SettingError::Ok);
    assert(near(s.tariff.vat_rate,0.255));
    assert(apply_site_setting(s,"vat_percent","60")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"transfer_kind","seasonal")==SettingError::Ok);
    assert(s.tariff.kind==TransferKind::Seasonal);
    assert(apply_site_setting(s,"transfer_kind","x")==SettingError::BadChoice);
    assert(apply_site_setting(s,"day_start_hour","6.5")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"components_include_vat","true")==SettingError::Ok &&
           s.tariff.components_include_vat);
    assert(apply_site_setting(s,"nope","1")==SettingError::UnknownKey);
    assert(apply_site_setting(s,"fmi_latlon","61.4,24.1")==SettingError::Ok);
    assert(apply_site_setting(s,"fmi_latlon","61,4,24,1")==SettingError::OutOfRange);
    // MET Norway Locationforecast covers the whole world, not just Finland.
    assert(apply_site_setting(s,"fmi_latlon","10,10")==SettingError::Ok);
    assert(apply_site_setting(s,"fmi_latlon","60,18")==SettingError::Ok);
    assert(apply_site_setting(s,"fmi_latlon","58.9,24")==SettingError::Ok);
    assert(apply_site_setting(s,"fmi_latlon","59,19")==SettingError::Ok);
    assert(apply_site_setting(s,"fmi_latlon","71,32")==SettingError::Ok);
    assert(apply_site_setting(s,"fmi_latlon","-33.8688,151.2093")==SettingError::Ok);
    assert(apply_site_setting(s,"fmi_latlon","0,0")==SettingError::Ok);
    assert(apply_site_setting(s,"fmi_latlon","91,0")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"fmi_latlon","0,181")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"fmi_latlon","-91,0")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"fmi_latlon","0,-181")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"fmi_latlon","")==SettingError::Ok && !s.fmi_latlon[0]);
    assert(apply_site_setting(s,"cn105_url","http://192.168.1.20/events")==SettingError::Ok);
    assert(apply_site_setting(s,"cn105_url","http://user:pw@192.168.1.20/events")==
           SettingError::BadUrl);
    assert(apply_site_setting(s,"shelly_url","ftp://x")==SettingError::BadUrl);
    assert(apply_site_setting(s,"shelly_url","")==SettingError::Ok);
    assert(apply_site_setting(s,"min_price_advantage_percent","15")==SettingError::Ok);
    assert(near(s.min_price_advantage_fraction,0.15));
}
static void catalog() {
    using namespace enevina;
    assert(adapter_selectable_in_lite_firmware("mitsubishi_cn105_esphome"));
    // Research-only or not wired into this firmware: never selectable.
    assert(!adapter_selectable_in_lite_firmware("nibe_s_modbus_tcp"));
    assert(!adapter_selectable_in_lite_firmware("panasonic_heishamon_http"));
    assert(!adapter_selectable_in_lite_firmware("unknown"));
    assert(!adapter_selectable_in_lite_firmware(nullptr));
    for(const auto& e:adapter_catalog) {
        assert(e.id && e.manufacturer && e.models && e.measurements && e.commands &&
               e.freshness && e.evidence);
        if(e.maturity==AdapterMaturity::DocumentedOnly) {
            assert(e.setpoint_max_c==0 && !e.command_readback);
            assert(std::strcmp(e.commands,"not implemented")==0);
        }
        if(e.setpoint_max_c>0) assert(e.setpoint_min_c<e.setpoint_max_c && e.setpoint_step_c>0);
    }
    const auto* m=find_adapter("mitsubishi_cn105_esphome");
    assert(m && m->setpoint_min_c==16 && m->setpoint_max_c==26 && m->command_readback);
}

// ---- Regional setup (country, bidding zone, currency-scaled limits) ----
static void regional_setup() {
    SiteSettings s{};
    // Defaults reproduce the original Finland-only behaviour.
    assert(!std::strcmp(s.country,"FI") && !std::strcmp(s.price_zone,"FI"));
    assert(valid_site_settings(s) && s.setup_complete && !s.tariff_confirmed);
    const char* area=nullptr;const char* cur=nullptr;int off=0;
    assert(site_market_region(s,area,cur,off) && !std::strcmp(area,"FI") &&
           !std::strcmp(cur,"EUR") && off==2);

    // Multi-zone country: the zone is NOT guessed; no region until chosen.
    assert(apply_site_setting(s,"country","SE")==SettingError::Ok);
    assert(!s.price_zone[0] && valid_site_settings(s));
    assert(!site_market_region(s,area,cur,off));
    assert(apply_site_setting(s,"price_zone","NO1")==SettingError::BadChoice);
    assert(apply_site_setting(s,"price_zone","SE3")==SettingError::Ok);
    assert(site_market_region(s,area,cur,off) && !std::strcmp(area,"SE3") &&
           !std::strcmp(cur,"SEK") && off==1);
    // A zone from another country, unknown country, unsupported country.
    assert(apply_site_setting(s,"country","XX")==SettingError::BadChoice);
    assert(apply_site_setting(s,"country","ES")==SettingError::BadChoice);
    assert(!std::strcmp(s.country,"SE"));   // rejected input changes nothing

    // Country change clears amounts of the old contract, keeps other settings.
    SiteSettings f{};
    f.tariff.margin_eur_per_kwh=0.004;f.tariff.vat_rate=0.255;
    f.tariff_confirmed=true;
    std::strcpy(f.cn105_url,"http://192.168.1.20/events");
    std::strcpy(f.pump_adapter,"mitsubishi_cn105_esphome");
    assert(apply_site_setting(f,"country","FI")==SettingError::Ok);  // same country
    assert(f.tariff_confirmed && near(f.tariff.margin_eur_per_kwh,0.004));
    assert(apply_site_setting(f,"country","EE")==SettingError::Ok);
    assert(!std::strcmp(f.price_zone,"EE"));            // single zone is implied
    assert(!f.tariff_confirmed && f.tariff.margin_eur_per_kwh==0 && f.tariff.vat_rate==0);
    assert(!std::strcmp(f.cn105_url,"http://192.168.1.20/events"));
    assert(!std::strcmp(f.pump_adapter,"mitsubishi_cn105_esphome"));

    // Currency-scaled plausibility: 60 ore/kWh transfer is normal in SEK,
    // absurd in EUR; the same number is rejected for a EUR country.
    SiteSettings se{};
    assert(apply_site_setting(se,"country","SE")==SettingError::Ok);
    assert(apply_site_setting(se,"transfer_day_c_per_kwh","60")==SettingError::Ok);
    assert(apply_site_setting(se,"transfer_day_c_per_kwh","700")==SettingError::OutOfRange);
    assert(valid_site_settings(se));
    SiteSettings eur{};
    assert(apply_site_setting(eur,"transfer_day_c_per_kwh","60")==SettingError::OutOfRange);
    // Order-independent: SEK amounts followed by a switch to a EUR country
    // reset the amounts (never leaves SEK numbers under a EUR zone).
    assert(apply_site_setting(se,"country","DE")==SettingError::Ok);
    assert(se.tariff.transfer_day_eur_per_kwh==0 && valid_site_settings(se));
    // Direct struct tampering is still caught by the whole-struct check.
    se.tariff.transfer_day_eur_per_kwh=0.6;
    assert(!valid_site_settings(se));

    // City / timezone / language are display data, strictly checked.
    SiteSettings c{};
    assert(apply_site_setting(c,"city","Kangasala")==SettingError::Ok);
    assert(apply_site_setting(c,"city","Malm\xC3\xB6")==SettingError::Ok);
    assert(apply_site_setting(c,"city","<script>")==SettingError::BadChoice);
    assert(apply_site_setting(c,"city","bad\xC3")==SettingError::BadChoice);      // truncated UTF-8
    assert(apply_site_setting(c,"city","line\nbreak")==SettingError::BadChoice);
    assert(apply_site_setting(c,"city",
        "0123456789012345678901234567890123456789012345678")==SettingError::TooLong);
    assert(apply_site_setting(c,"timezone","Europe/Helsinki")==SettingError::Ok);
    assert(apply_site_setting(c,"timezone","Europe/../x y")==SettingError::BadChoice);
    assert(apply_site_setting(c,"language","fi")==SettingError::Ok);
    assert(apply_site_setting(c,"language","de")==SettingError::BadChoice);
    assert(apply_site_setting(c,"language","")==SettingError::Ok);
    assert(apply_site_setting(c,"setup_step","3")==SettingError::Ok && c.setup_step==3);
    assert(apply_site_setting(c,"setup_step","10")==SettingError::OutOfRange);
    assert(apply_site_setting(c,"setup_complete","false")==SettingError::Ok && !c.setup_complete);
    assert(apply_site_setting(c,"setup_complete","1")==SettingError::BadChoice);
    // Unterminated stored text is rejected by validation (corrupt NVS blob).
    SiteSettings bad{};
    std::memset(bad.city,'a',sizeof(bad.city));
    assert(!valid_site_settings(bad));
    SiteSettings badzone{};
    std::strcpy(badzone.country,"SE");std::strcpy(badzone.price_zone,"NO1");
    assert(!valid_site_settings(badzone));
}

// The transfer schedule follows the civil time of the SELECTED country.
static void transfer_schedule_uses_country_time() {
    SiteSettings se{};
    assert(apply_site_setting(se,"country","SE")==SettingError::Ok);
    assert(apply_site_setting(se,"price_zone","SE3")==SettingError::Ok);
    se.tariff.kind=TransferKind::TimeOfDay;
    se.tariff.day_start_hour=6;se.tariff.night_start_hour=22;
    se.tariff.transfer_day_eur_per_kwh=0.40;se.tariff.transfer_night_eur_per_kwh=0.10;
    se.tariff.vat_rate=0;
    double total=0;
    // 2026-01-12 05:30Z = 06:30 in Sweden (UTC+1) = 07:30 in Finland (UTC+2).
    // Day tariff starts 06:00 local: Sweden is day, a 07:00..22:00 Finnish
    // schedule would also say day, but at 05:00Z (06:00 SE / 07:00 FI) the
    // boundary matters: 04:45Z = 05:45 SE (night) = 06:45 FI.
    assert(!site_region_total_price(se,1768193100,0.0,total));
    se.tariff_confirmed=true;
    assert(site_region_total_price(se,1768193100,0.0,total));   // 04:45Z -> 05:45 SE
    assert(near(total,0.10));
    assert(site_region_total_price(se,1768194000,0.0,total));   // 05:00Z -> 06:00 SE
    assert(near(total,0.40));
    // Summer time: 2026-07-01 04:00Z = 06:00 CEST in Sweden.
    assert(site_region_total_price(se,1782878400,0.0,total));
    assert(near(total,0.40));
    assert(site_region_total_price(se,1782874800,0.0,total));   // 03:00Z = 05:00 CEST
    assert(near(total,0.10));
    // Same instant is a different local hour in Finland (UTC+2).
    SiteSettings fi=se;
    assert(apply_site_setting(fi,"country","FI")==SettingError::Ok);
    fi.tariff=se.tariff;
    assert(!site_region_total_price(fi,1768193100,0.0,total));
    fi.tariff_confirmed=true;
    assert(site_region_total_price(fi,1768193100,0.0,total));   // 06:45 FI -> day
    assert(near(total,0.40));
}


// Upgrading a Finland-only device keeps every configured value and does not
// send the owner back through the first-start wizard.
static void v1_migration() {
    SiteSettingsV1 old{};
    old.tariff.margin_eur_per_kwh=0.0049;old.tariff.transfer_day_eur_per_kwh=0.0345;
    old.tariff.transfer_night_eur_per_kwh=0.0205;old.tariff.electricity_tax_eur_per_kwh=0.02827;
    old.tariff.vat_rate=0.255;old.tariff.components_include_vat=true;
    old.tariff.kind=TransferKind::Seasonal;
    std::strcpy(old.cn105_url,"http://192.168.1.20/events");
    std::strcpy(old.shelly_url,"http://192.168.1.30");
    std::strcpy(old.fmi_latlon,"61.4,24.1");
    std::strcpy(old.pump_adapter,"mitsubishi_cn105_esphome");
    old.min_price_advantage_fraction=0.15;
    const SiteSettings m=site_settings_from_v1(old);
    assert(valid_site_settings(m));
    assert(near(m.tariff.margin_eur_per_kwh,0.0049)&&near(m.tariff.vat_rate,0.255));
    assert(m.tariff.components_include_vat&&m.tariff.kind==TransferKind::Seasonal);
    assert(!std::strcmp(m.cn105_url,"http://192.168.1.20/events"));
    assert(!std::strcmp(m.shelly_url,"http://192.168.1.30"));
    assert(!std::strcmp(m.fmi_latlon,"61.4,24.1"));
    assert(!std::strcmp(m.pump_adapter,"mitsubishi_cn105_esphome"));
    assert(near(m.min_price_advantage_fraction,0.15));
    assert(!std::strcmp(m.country,"FI")&&!std::strcmp(m.price_zone,"FI"));
    assert(m.setup_complete&&m.tariff_confirmed);        // no wizard on upgrade
    const char* area=nullptr;const char* cur=nullptr;int off=0;
    assert(site_market_region(m,area,cur,off)&&!std::strcmp(area,"FI"));
    // Same total price as the original Finland-only rule.
    double a=0,b=0;
    assert(site_total_price(old.tariff,1768212000,0.10,a));
    assert(site_region_total_price(m,1768212000,0.10,b)&&near(a,b));
}


// The wizard may send fields in any order: the amount limits must follow the
// NEW country's currency, never the previous one.
static void batch_is_order_independent() {
    const char* k1[]={"transfer_day_c_per_kwh","price_zone","country","margin_c_per_kwh"};
    const char* v1[]={"60","SE3","SE","1.5"};
    SiteSettings a{};
    size_t bad=0;
    assert(apply_site_settings_batch(a,4,k1,v1,bad)==SettingError::Ok);
    assert(!std::strcmp(a.country,"SE")&&!std::strcmp(a.price_zone,"SE3"));
    assert(near(a.tariff.transfer_day_eur_per_kwh,0.60)&&valid_site_settings(a));
    // Reordered: same result.
    const char* k2[]={"country","margin_c_per_kwh","price_zone","transfer_day_c_per_kwh"};
    const char* v2[]={"SE","1.5","SE3","60"};
    SiteSettings b{};
    assert(apply_site_settings_batch(b,4,k2,v2,bad)==SettingError::Ok);
    assert(near(b.tariff.transfer_day_eur_per_kwh,a.tariff.transfer_day_eur_per_kwh));
    // A zone from a different country is rejected and reported by index.
    const char* k3[]={"margin_c_per_kwh","price_zone","country"};
    const char* v3[]={"1","NO1","SE"};
    SiteSettings c{};
    assert(apply_site_settings_batch(c,3,k3,v3,bad)==SettingError::BadChoice&&bad==1);
    // The same 60 under a EUR country is out of range.
    const char* k4[]={"country","transfer_day_c_per_kwh"};
    const char* v4[]={"DE","60"};
    SiteSettings d{};
    assert(apply_site_settings_batch(d,2,k4,v4,bad)==SettingError::OutOfRange&&bad==1);
}


static void profile_and_comfort_limits() {
    SiteSettings s{};
    assert(s.profile==OptimizationProfile::Balanced&&s.comfort_min_c==0&&s.comfort_max_c==0);
    assert(valid_site_settings(s));
    assert(apply_site_setting(s,"optimization_profile","saving")==SettingError::Ok&&
           s.profile==OptimizationProfile::Saving);
    assert(apply_site_setting(s,"optimization_profile","eco")==SettingError::BadChoice);
    assert(s.profile==OptimizationProfile::Saving);              // rejected input changes nothing
    assert(apply_site_setting(s,"comfort_min_c","19")==SettingError::Ok);
    assert(apply_site_setting(s,"comfort_max_c","23,5")==SettingError::Ok);
    assert(near(s.comfort_min_c,19)&&near(s.comfort_max_c,23.5)&&valid_site_settings(s));
    assert(apply_site_setting(s,"comfort_min_c","4")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"comfort_max_c","27")==SettingError::OutOfRange);
    assert(apply_site_setting(s,"comfort_min_c","19.3")==SettingError::OutOfRange);   // 0.5 steps
    assert(apply_site_setting(s,"comfort_min_c","abc")==SettingError::BadNumber);
    assert(apply_site_setting(s,"comfort_min_c","")==SettingError::Ok&&s.comfort_min_c==0);  // clears
    // Whole-struct check catches limits that cross after separate edits.
    s.comfort_min_c=23;s.comfort_max_c=19;
    assert(!valid_site_settings(s));
    s.comfort_min_c=20;s.comfort_max_c=20.5;
    assert(!valid_site_settings(s));                              // need >= 1 C of band
    s.comfort_min_c=0;s.comfort_max_c=0;s.profile=static_cast<OptimizationProfile>(7);
    assert(!valid_site_settings(s));
    // Upgraded devices keep the pre-profile behaviour.
    SiteSettingsV1 old{};
    const SiteSettings m=site_settings_from_v1(old);
    assert(m.profile==OptimizationProfile::Balanced&&m.comfort_min_c==0&&m.comfort_max_c==0);
}

static void older_layout_is_restored_not_reset() {
    // Codex P1 (#57): a blob written before profiles existed used to fail an
    // exact-size check, silently resetting pump endpoint, location and tariff.
    SiteSettings full{};
    std::strcpy(full.cn105_url,"http://192.168.1.50/events");
    std::strcpy(full.pump_adapter,"mitsubishi_cn105");
    std::strcpy(full.country,"SE");std::strcpy(full.price_zone,"SE3");
    full.tariff_confirmed=true;full.setup_complete=true;full.setup_step=9;
    full.tariff.margin_eur_per_kwh=0.005;
    full.profile=OptimizationProfile::Saving;full.comfort_min_c=19;full.comfort_max_c=23;
    uint8_t buf[sizeof(SiteSettings)];
    std::memcpy(buf,&full,sizeof(buf));
    const size_t boundary=kSettingsLayoutBoundaries[0];
    const size_t align=alignof(SiteSettings);
    const size_t old_size=(boundary+align-1)/align*align;
    // poison everything the old firmware could not have written
    std::memset(buf+boundary,0xA5,sizeof(buf)-boundary);
    SiteSettings out{};
    assert(restore_site_settings(buf,old_size,out));
    assert(std::strcmp(out.cn105_url,"http://192.168.1.50/events")==0);
    assert(std::strcmp(out.pump_adapter,"mitsubishi_cn105")==0);
    assert(std::strcmp(out.country,"SE")==0&&std::strcmp(out.price_zone,"SE3")==0);
    assert(out.tariff_confirmed&&out.setup_complete&&out.setup_step==9);
    assert(near(out.tariff.margin_eur_per_kwh,0.005));
    // newer members take their defaults, never the poison bytes
    assert(out.profile==OptimizationProfile::Balanced&&out.comfort_min_c==0&&out.comfort_max_c==0);
    assert(valid_site_settings(out));
    // the current layout restores whole
    std::memcpy(buf,&full,sizeof(buf));
    assert(restore_site_settings(buf,sizeof(buf),out)&&out.profile==OptimizationProfile::Saving&&near(out.comfort_max_c,23));
    // sizes that match no known layout are refused (corrupt or from newer firmware)
    assert(!restore_site_settings(buf,sizeof(buf)+8,out));
    assert(!restore_site_settings(buf,boundary-1,out));
    assert(!restore_site_settings(buf,0,out));
    assert(!restore_site_settings(nullptr,sizeof(buf),out));
    if(old_size+1<sizeof(buf)&&old_size+1>(boundary+align-1)/align*align) assert(!restore_site_settings(buf,old_size+1,out));
}

static void rollback_edit_overrides_stale_v3() {
    // Simulate: v3 candidate saves the unchanged old v2 snapshot, then
    // rollback firmware edits v2 and the new candidate boots again.
    uint8_t v2[80]{};
    v2[0]=0x45;v2[1]=0x53;v2[2]=0x54;v2[3]=0x31;
    const uint64_t candidate_baseline=site_v2_signature(v2,sizeof(v2));
    assert(candidate_baseline!=0);
    assert(!site_rollback_v2_modified(candidate_baseline,v2,sizeof(v2)));
    v2[36]^=0x5a; // changed owner's endpoint/tariff/location payload
    assert(site_rollback_v2_modified(candidate_baseline,v2,sizeof(v2)));
    assert(!site_rollback_v2_modified(candidate_baseline,nullptr,0));
    assert(site_rollback_v2_modified(0,v2,sizeof(v2)));
    // New v4 holds separate baselines for rollback v2 and rollback v3.
    uint8_t v3[96]{};
    v3[2]=3;
    const uint64_t baseline_v3=site_v2_signature(v3,sizeof(v3));
    assert(!site_rollback_v2_modified(baseline_v3,v3,sizeof(v3)));
    v3[40]=0x7a;
    assert(site_rollback_v2_modified(baseline_v3,v3,sizeof(v3)));
}

static void v3_provenance_after_rollback_then_v4_upgrade() {
    // First v4 boot: a signature-bearing v3 snapshot is valid, but the
    // intervening v2 rollback owner edit must take priority.
    uint8_t v2[96]{};
    uint8_t v3[8+96+sizeof(uint64_t)]{};
    const uint16_t version=3,settings_len=96;
    std::memcpy(v3+4,&version,sizeof(version));
    std::memcpy(v3+6,&settings_len,sizeof(settings_len));
    const uint64_t v2_at_v3_save=site_v2_signature(v2,sizeof(v2));
    std::memcpy(v3+8+96,&v2_at_v3_save,sizeof(v2_at_v3_save));
    assert(!site_v3_prefer_rollback_v2(v3,sizeof(v3),96,v2,sizeof(v2)));
    v2[55]^=0x3d;  // previous firmware changes a real saved setting
    assert(site_v3_prefer_rollback_v2(v3,sizeof(v3),96,v2,sizeof(v2)));
    // A pre-signature v3 never claims to know which record is newer.
    assert(!site_v3_prefer_rollback_v2(v3,sizeof(v3)-8,96,v2,sizeof(v2)));
    assert(!site_v3_prefer_rollback_v2(v3,7,0,v2,sizeof(v2)));
    assert(!site_v3_prefer_rollback_v2(nullptr,sizeof(v3),96,v2,sizeof(v2)));
}
struct FaultingSnapshot {
    uint8_t data[32]{};
    bool exists=true,read_error=false,zero_length_error=false;
    size_t getBytesLength(const char*) const {return zero_length_error?0:exists?sizeof(data):0;}
    bool isKey(const char*) const {return exists;}
    size_t getBytes(const char*,void* out,size_t n) const {
        if(read_error)return 0;
        std::memcpy(out,data,n);
        return n;
    }
};
static void nvs_snapshot_failure_does_not_commit_an_unknown_baseline() {
    FaultingSnapshot nvs;
    uint8_t scratch[64]{};
    uint64_t signature=123;
    assert(site_rollback_snapshot_signature(nvs,"v2",signature,scratch,sizeof(scratch)));
    const uint64_t normal=signature;
    assert(normal!=0);
    nvs.read_error=true;
    assert(!site_rollback_snapshot_signature(nvs,"v2",signature,scratch,sizeof(scratch)));
    assert(signature==0);   // must not be treated as an absent clean key
    nvs.read_error=false;
    nvs.zero_length_error=true;
    assert(!site_rollback_snapshot_signature(nvs,"v2",signature,scratch,sizeof(scratch)));
    nvs.zero_length_error=false;
    assert(!site_rollback_snapshot_signature(nvs,"v2",signature,scratch,4));
    nvs.exists=false;
    assert(site_rollback_snapshot_signature(nvs,"v2",signature,scratch,sizeof(scratch)));
    assert(signature==0); // true absence is allowed
}

// Crash-recovery journal phase invariants, independent of the device NVS.
static void comfort_transaction_survives_site_air_split() {
    ComfortTxn tx{};
    tx.mode=2;tx.profile=static_cast<uint8_t>(OptimizationProfile::Saving);
    tx.previous_profile=static_cast<uint8_t>(OptimizationProfile::Balanced);
    tx.previous_setup_step=5;
    tx.previous_target=12;
    tx.target=20;tx.min_c=19;tx.max_c=23;
    tx.previous_min_c=9;tx.previous_max_c=14;
    assert(valid_comfort_txn(tx));
    SiteSettings before{},after{};
    before.setup_step=5;
    before.profile=OptimizationProfile::Balanced;
    before.comfort_min_c=9;before.comfort_max_c=14;
    after.setup_step=6;
    after.profile=OptimizationProfile::Saving;
    after.comfort_min_c=19;after.comfort_max_c=23;
    CottageModeConfig air{};
    air.mode=CottageMode::Hold;air.hold_target_c=12;
    assert(!comfort_site_matches(tx,before));
    assert(!comfort_site_matches(tx,after));
    assert(comfort_old_site_matches(tx,before));
    assert(comfort_old_mode_matches(tx,air));
    tx.phase=static_cast<uint8_t>(ComfortPhase::SiteCommitted);
    assert(valid_comfort_txn(tx));
    assert(comfort_site_matches(tx,after));
    assert(!comfort_mode_matches(tx,air));
    comfort_apply_target(tx,air);
    assert(comfort_mode_matches(tx,air));
    const double previous=air.hold_target_c;
    comfort_apply_target(tx,air);
    assert(air.hold_target_c==previous);
    // Changing only the target cannot be mistaken for a saved site blob:
    // unchanged limits are not a durable commit marker.
    tx.target=13;tx.previous_target=12;
    tx.min_c=9;tx.max_c=14;
    tx.previous_profile=tx.profile;
    tx.previous_setup_step=6;
    tx.previous_min_c=9;tx.previous_max_c=14;
    SiteSettings same=after;
    same.comfort_min_c=9;same.comfort_max_c=14;
    assert(valid_comfort_txn(tx));
    tx.phase=static_cast<uint8_t>(ComfortPhase::Prepared);
    assert(!comfort_site_matches(tx,same));
    assert(comfort_old_site_matches(tx,same));
    tx.phase=static_cast<uint8_t>(ComfortPhase::SiteCommitted);
    assert(comfort_site_matches(tx,same));
    tx.magic=0;assert(!valid_comfort_txn(tx));
}
static void comfort_journal_power_loss_matrix() {
    // All crash points from initial marker, site commit, air partial write,
    // subsequent new mode, and unreadable NVS. Pure firmware recovery policy
    // is tested without assuming that Preferences putBytes succeeded.
    ComfortTxn tx{};
    tx.mode=2;tx.profile=uint8_t(OptimizationProfile::Saving);
    tx.previous_profile=uint8_t(OptimizationProfile::Balanced);
    tx.previous_setup_step=6;
    tx.previous_target=12;tx.previous_min_c=9;tx.previous_max_c=14;
    tx.target=21;tx.min_c=19;tx.max_c=23;
    SiteSettings site{};
    site.setup_step=6;site.profile=OptimizationProfile::Saving;
    site.comfort_min_c=19;site.comfort_max_c=23;
    CottageModeConfig air{};
    air.mode=CottageMode::Hold;air.hold_target_c=12;
    using Action=ComfortRecoveryAction;
    // Before site persist or before phase transition, never replay target.
    tx.phase=uint8_t(ComfortPhase::Prepared);
    assert(comfort_recovery_action(tx,site,air,true)==Action::RestorePriorSite);
    // Verified phase transitions to air completion only after commit evidence.
    tx.phase=uint8_t(ComfortPhase::SiteCommitted);
    assert(comfort_recovery_action(tx,site,air,true)==Action::CompleteAirTarget);
    // At second air commit old or partially updated target is still recovered
    // only if the versioned site intent is proven committed.
    air.hold_target_c=21;
    assert(comfort_recovery_action(tx,site,air,true)==Action::CompleteAirTarget);
    // Source NVS corruption or a partially written mode image NEVER recovers.
    assert(comfort_recovery_action(tx,site,air,false)==Action::Block);
    tx.magic=0;
    assert(comfort_recovery_action(tx,site,air,true)==Action::Block);
    tx.magic=0x43545831;
    // Owner's later different mode cannot be overwritten by old target.
    air.mode=CottageMode::Occupied;
    assert(comfort_recovery_action(tx,site,air,true)==Action::RestorePriorSite);
    air.mode=CottageMode::Hold;
    site.profile=OptimizationProfile::Balanced;
    assert(comfort_recovery_action(tx,site,air,true)==Action::RestorePriorSite);
    // Site match is not enough if phase never changed (target-only case).
    tx.phase=uint8_t(ComfortPhase::Prepared);
    assert(comfort_recovery_action(tx,site,air,true)==Action::RestorePriorSite);
}
static void hard_comfort_limits_never_silently_disappear() {
    assert(comfort_limits_fit_target(19,23,21));
    assert(!comfort_limits_fit_target(19,23,12));
    assert(!comfort_limits_fit_target(19,23,23));
    assert(comfort_limits_fit_target(0,0,12));
    assert(comfort_limits_fit_target(11.5,12.5,12));
    assert(!comfort_limits_fit_target(12,13,12));
    assert(!comfort_limits_fit_target(11,12,12));
    SiteSettings s{};
    s.comfort_min_c=19;s.comfort_max_c=23;
    CottageModeConfig c{};
    c.mode=CottageMode::Hold;c.hold_target_c=12;
    assert(!mode_comfort_limits_fit(s,c));
    c.hold_target_c=21;
    assert(mode_comfort_limits_fit(s,c));
    c.mode=CottageMode::Occupied;c.occupied_target_c=17;
    assert(!mode_comfort_limits_fit(s,c));
    c.occupied_target_c=21;
    assert(mode_comfort_limits_fit(s,c));
    c.mode=CottageMode::Cold;
    assert(mode_comfort_limits_fit(s,c));
    c.mode=CottageMode::Drying;
    assert(mode_comfort_limits_fit(s,c));
}

int main() {
    comfort_transaction_survives_site_air_split();
    hard_comfort_limits_never_silently_disappear();
    comfort_journal_power_loss_matrix();
    v3_provenance_after_rollback_then_v4_upgrade();
    nvs_snapshot_failure_does_not_commit_an_unknown_baseline();
    rollback_edit_overrides_stale_v3();
    older_layout_is_restored_not_reset();
    total_price_rules();
    seasonal_and_dst();
    parsing();
    catalog();
    regional_setup();
    v1_migration();
    profile_and_comfort_limits();
    batch_is_order_independent();
    transfer_schedule_uses_country_time();
    std::puts("site_settings_tests OK");
}
