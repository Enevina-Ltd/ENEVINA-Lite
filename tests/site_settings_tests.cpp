#include "cottage/site_settings.hpp"
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

int main() {
    total_price_rules();
    seasonal_and_dst();
    parsing();
    catalog();
    regional_setup();
    v1_migration();
    batch_is_order_independent();
    transfer_schedule_uses_country_time();
    std::puts("site_settings_tests OK");
}
