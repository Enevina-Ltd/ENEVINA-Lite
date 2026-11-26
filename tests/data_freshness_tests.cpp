// Freshness/offline report must agree with the live controller in every scenario.
#include "cottage/data_freshness.hpp"
#include "cottage/price_bootstrap.hpp"
#include "forecast/met_norway.hpp"      // strict JSON validator
#include <cstdio>
#include <string>
#include <vector>

using namespace enevina::cottage;
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)

constexpr int64_t NOW=1800000000;
static FreshnessInputs healthy() {
    FreshnessInputs in;
    in.clock_trusted=true;in.now=NOW;in.wifi_ok=true;
    in.price_zone_configured=true;in.price_current_valid=true;in.price_decision_valid=true;
    in.price_last_fetch=NOW-600;in.price_valid_until=NOW+20*3600;
    in.weather_configured=true;in.weather_available=true;in.weather_fetched=NOW-1800;in.weather_valid_until=NOW+30*3600;
    in.sensor_bound=in.sensor_fresh=true;in.sensor_age_s=20;
    in.pump_configured=in.pump_fresh=true;in.pump_age_s=5;
    return in;
}

// What the controller does with the same facts (mirrors main.cpp's eligibility).
static BootstrapDecision controller(const FreshnessInputs& in) {
    if(in.no_heating_requested) {
        BootstrapDecision d{};
        d.action=BootstrapAction::Off;
        d.reason=BootstrapReason::NoHeatingRequested;
        return d;
    }
    BootstrapInput b{};b.heating={16,26,0.5,true,true};
    b.indoor_fresh=in.sensor_fresh;b.indoor_c=20.0;b.desired_room_c=21.0;
    static const double prices[8]={0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1};
    const bool eligible=in.clock_trusted&&in.price_current_valid&&in.price_last_fetch<=in.now&&
        in.price_last_fetch>=in.now-36*3600&&in.price_valid_until>=(in.now/900)*900+8*900&&
        in.price_decision_valid;
    b.next_total_prices=prices;b.price_count=8;b.prices_contiguous_and_fresh=eligible;
    return bootstrap_price_decision(b);
}
static void agrees_with_controller(const FreshnessInputs& in,ControllerFallback expect) {
    const Freshness f=assess_freshness(in);
    CHECK(f.fallback==expect);
    const BootstrapDecision d=controller(in);
    switch(f.fallback) {
    case ControllerFallback::PriceControl:CHECK(d.price_data_valid&&d.action!=BootstrapAction::Unavailable);break;
    case ControllerFallback::Thermostat:CHECK(!d.price_data_valid&&d.action!=BootstrapAction::Unavailable);break;
    case ControllerFallback::Blocked:CHECK(d.action==BootstrapAction::Unavailable);break;
    case ControllerFallback::NoHeatingRequested:
        CHECK(d.action==BootstrapAction::Off&&d.reason==BootstrapReason::NoHeatingRequested);break;
    }
}

static void no_heating_mode_report_is_not_thermostat() {
    auto in=healthy();
    in.no_heating_requested=true;
    CHECK(assess_freshness(in).fallback==ControllerFallback::NoHeatingRequested);
    agrees_with_controller(in,ControllerFallback::NoHeatingRequested);
    in.price_decision_valid=false;in.price_current_valid=false;
    CHECK(assess_freshness(in).fallback==ControllerFallback::NoHeatingRequested);
    agrees_with_controller(in,ControllerFallback::NoHeatingRequested);
    // Cold mode remains OFF even without a room sensor; it does not
    // become an active thermostat simply because prices are missing.
    in.sensor_fresh=false;
    CHECK(assess_freshness(in).fallback==ControllerFallback::NoHeatingRequested);
    agrees_with_controller(in,ControllerFallback::NoHeatingRequested);
    in.no_heating_requested=false;
    CHECK(assess_freshness(in).fallback==ControllerFallback::Blocked);
}
static void healthy_device() {
    const Freshness f=assess_freshness(healthy());
    CHECK(f.prices.state==SourceState::Ok&&f.weather.state==SourceState::Ok&&f.sensor.state==SourceState::Ok&&f.pump.state==SourceState::Ok);
    CHECK(!f.offline&&f.clock_ok&&f.fallback==ControllerFallback::PriceControl);
    CHECK(f.last_contact_utc==NOW-600);                     // newest of prices/weather
    CHECK(f.next_expiry_utc==NOW+20*3600);                  // prices end first
    agrees_with_controller(healthy(),ControllerFallback::PriceControl);
}

static void going_offline_changes_nothing_until_data_expires() {
    FreshnessInputs in=healthy();in.wifi_ok=false;
    Freshness f=assess_freshness(in);
    CHECK(f.offline&&f.prices.state==SourceState::Ok&&f.fallback==ControllerFallback::PriceControl);
    // 12 hours later, still offline: prices (valid 20 h) still usable, weather 30 h
    in.now=NOW+12*3600;
    f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::Ok&&f.weather.state==SourceState::Ok&&f.fallback==ControllerFallback::PriceControl);
    agrees_with_controller(in,ControllerFallback::PriceControl);
    // 19.5 h: prices are about to end -> Short -> controller falls back already
    in.now=NOW+static_cast<int64_t>(19.5*3600);
    f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::Short&&f.fallback==ControllerFallback::Thermostat);
    agrees_with_controller(in,ControllerFallback::Thermostat);
    // 21 h: prices expired; weather still fine (stale prices are never used as new)
    in.now=NOW+21*3600;in.price_current_valid=false;
    f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::Expired&&f.weather.state==SourceState::Ok&&f.fallback==ControllerFallback::Thermostat);
    agrees_with_controller(in,ControllerFallback::Thermostat);
    // 31 h: forecast expired too; the sensors keep the room under the thermostat
    in.now=NOW+31*3600;in.weather_available=false;
    f=assess_freshness(in);
    CHECK(f.weather.state==SourceState::Expired&&f.next_expiry_utc==0);
    CHECK(f.last_contact_utc==NOW-600);                     // "last successful contact" stays the old one
}

static void quarter_hour_alignment() {
    FreshnessInputs in=healthy();
    // A full 8 slots remain from the START of the current quarter hour.
    in.now=NOW+60;in.price_valid_until=NOW+8*900;
    CHECK(assess_freshness(in).prices.state==SourceState::Ok);
    agrees_with_controller(in,ControllerFallback::PriceControl);
    in.now=NOW+899;
    CHECK(assess_freshness(in).fallback==ControllerFallback::PriceControl);
    agrees_with_controller(in,ControllerFallback::PriceControl);
    in.now=NOW+900;
    CHECK(assess_freshness(in).prices.state==SourceState::Short);
    agrees_with_controller(in,ControllerFallback::Thermostat);
}

static void thirty_six_hour_rule() {
    FreshnessInputs in=healthy();
    in.price_last_fetch=NOW-37*3600;in.price_valid_until=NOW+10*3600;   // still valid by the calendar, fetched too long ago
    Freshness f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::Stale&&f.fallback==ControllerFallback::Thermostat);
    agrees_with_controller(in,ControllerFallback::Thermostat);
    in.price_last_fetch=NOW-36*3600;                                      // exactly at the limit is still allowed
    CHECK(assess_freshness(in).prices.state==SourceState::Ok);
    agrees_with_controller(in,ControllerFallback::PriceControl);
    in.price_last_fetch=NOW+60;                                           // a fetch time in the future is not trusted
    CHECK(assess_freshness(in).prices.state!=SourceState::Ok);
    agrees_with_controller(in,ControllerFallback::Thermostat);
}

static void missing_and_unconfigured_sources() {
    FreshnessInputs in=healthy();
    in.price_zone_configured=false;
    Freshness f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::NotConfigured&&f.fallback==ControllerFallback::Thermostat);
    in=healthy();in.price_last_fetch=0;in.price_current_valid=false;in.price_valid_until=0;
    CHECK(assess_freshness(in).prices.state==SourceState::Missing);
    in.price_error=103;                                                    // transport failure, never had prices
    f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::Error&&f.prices.error==103);
    in=healthy();in.price_current_valid=false;in.price_error=104;          // had prices, now failing
    f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::Error&&f.prices.last_ok_utc==NOW-600);   // the last good time stays visible
    in=healthy();in.weather_configured=false;
    CHECK(assess_freshness(in).weather.state==SourceState::NotConfigured&&assess_freshness(in).fallback==ControllerFallback::PriceControl);
}

static void actual_price_eligibility_and_pump_age() {
    FreshnessInputs in=healthy();
    // Spot metadata may be fresh even if the owner's tariff is not confirmed
    // or a slot in the next 2h is missing. Actual bootstrap says false.
    in.price_decision_valid=false;
    const Freshness f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::Ok);
    CHECK(f.fallback==ControllerFallback::Thermostat);
    agrees_with_controller(in,ControllerFallback::Thermostat);
    CHECK(verified_pump_age(NOW,true,NOW-5,false,NOW-5000)==5);
    CHECK(verified_pump_age(NOW,false,NOW-5000,true,NOW-7)==7);
    CHECK(verified_pump_age(NOW,false,0,false,0)==-1);
}
static void sensor_and_pump() {
    FreshnessInputs in=healthy();
    in.sensor_fresh=false;in.sensor_age_s=900;
    Freshness f=assess_freshness(in);
    CHECK(f.sensor.state==SourceState::Stale&&f.sensor.age_s==900&&f.fallback==ControllerFallback::Blocked);
    agrees_with_controller(in,ControllerFallback::Blocked);
    in=healthy();in.sensor_bound=false;in.sensor_fresh=false;
    CHECK(assess_freshness(in).sensor.state==SourceState::NotConfigured&&assess_freshness(in).fallback==ControllerFallback::Blocked);
    in=healthy();in.pump_fresh=false;in.pump_age_s=600;
    f=assess_freshness(in);
    CHECK(f.pump.state==SourceState::Stale&&f.fallback==ControllerFallback::PriceControl);   // pump data does not gate the proposal
}

static void untrusted_clock() {
    FreshnessInputs in=healthy();in.clock_trusted=false;
    Freshness f=assess_freshness(in);
    CHECK(!f.clock_ok&&f.prices.state==SourceState::Stale&&f.weather.state==SourceState::Stale);
    CHECK(f.fallback==ControllerFallback::Thermostat);
    agrees_with_controller(in,ControllerFallback::Thermostat);
    in.price_last_fetch=0;in.weather_fetched=0;
    f=assess_freshness(in);
    CHECK(f.prices.state==SourceState::Missing&&f.weather.state==SourceState::Missing&&f.next_expiry_utc==0);
}

static void json_output() {
    const Freshness f=assess_freshness(healthy());
    std::string out;
    freshness_json(f,true,"READY",[&](const char* p){out+=p;});
    CHECK(out.find("\"control_gate\":\"READY\"")!=std::string::npos);
    enevina::met_detail::Json v{out.data(),out.data()+out.size()};
    CHECK(v.value());v.ws();CHECK(v.cur==v.end);
    CHECK(out.find("\"fallback\":\"price_control\"")!=std::string::npos&&out.find("\"control_armed\":true")!=std::string::npos);
    CHECK(out.find("\"id\":\"prices\"")!=std::string::npos&&out.find("\"id\":\"pump\"")!=std::string::npos);
    CHECK(out.find("nan")==std::string::npos);
    FreshnessInputs bad=healthy();bad.wifi_ok=false;bad.price_current_valid=false;bad.price_error=103;
    std::string o2;
    freshness_json(assess_freshness(bad),false,"DISABLED",[&](const char* p){o2+=p;});
    CHECK(o2.find("\"control_gate\":\"DISABLED\"")!=std::string::npos);
    enevina::met_detail::Json v2{o2.data(),o2.data()+o2.size()};
    CHECK(v2.value()&&o2.find("\"offline\":true")!=std::string::npos&&o2.find("\"state\":\"error\"")!=std::string::npos);
}

int main() {
    no_heating_mode_report_is_not_thermostat();
    healthy_device();
    going_offline_changes_nothing_until_data_expires();
    thirty_six_hour_rule();
    quarter_hour_alignment();
    missing_and_unconfigured_sources();
    actual_price_eligibility_and_pump_age();
    sensor_and_pump();
    untrusted_clock();
    json_output();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("data_freshness_tests OK");
    return 0;
}
