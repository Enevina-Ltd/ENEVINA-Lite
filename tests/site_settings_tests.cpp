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
    assert(apply_site_setting(s,"fmi_latlon","10,10")==SettingError::OutOfRange);
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
int main() {
    total_price_rules();
    seasonal_and_dst();
    parsing();
    catalog();
    std::puts("site_settings_tests OK");
}
