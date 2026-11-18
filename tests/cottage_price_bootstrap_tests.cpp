#include "cottage/price_bootstrap.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <cstring>
using namespace enevina::cottage;
static bool near(double a,double b){return std::fabs(a-b)<1e-8;}
int main() {
    const double cheap[]={0.10,0.20,0.30,0.40,0.50,0.60,0.70,0.80};
    const double expensive[]={0.80,0.10,0.20,0.30,0.40,0.50,0.60,0.70};
    BootstrapInput in{};
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.indoor_fresh=true;in.indoor_c=15.0;
    auto no_prices=bootstrap_price_decision(in);
    assert(no_prices.action==BootstrapAction::HeatSetpoint);
    assert(no_prices.reason==BootstrapReason::ThermostatWithoutPrices);
    assert(!no_prices.price_data_valid);
    assert(near(no_prices.heat_setpoint_c,16.5)); // default target is 16C
    in.next_total_prices=cheap;in.price_count=8;
    in.prices_contiguous_and_fresh=true;in.desired_room_c=18.0;
    auto d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::HeatSetpoint && !d.executable);
    assert(d.reason==BootstrapReason::TemperatureLow);
    assert(std::strcmp(bootstrap_reason_name(d.reason),"BELOW_ROOM_TARGET")==0);
    assert(near(d.requested_room_c,18.0));
    assert(near(d.heat_setpoint_c,20.0));
    assert(near(d.compared_median_eur_per_kwh,0.45));
    in.next_total_prices=expensive;
    d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::HeatSetpoint);
    assert(near(d.heat_setpoint_c,18.5));
    in.indoor_c=18.7;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Off);
    assert(std::strcmp(bootstrap_reason_name(
        bootstrap_price_decision(in).reason),"ABOVE_ROOM_TARGET")==0);
    in.indoor_c=18.0;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ExpensiveWindow);
    // Split deadband (Codex P1, PR #28): cheap pre-heat only below target,
    // expensive coast only at/above target, otherwise hold the current mode.
    in.next_total_prices=cheap;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::DeadbandHold);
    in.indoor_c=17.9;
    d=bootstrap_price_decision(in);
    assert(d.reason==BootstrapReason::CheapWindow);
    assert(near(d.heat_setpoint_c,19.0));
    in.next_total_prices=expensive;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    assert(std::strcmp(bootstrap_reason_name(
        bootstrap_price_decision(in).reason),"HOLD_IN_DEADBAND")==0);
    // Codex repro: target 12, prices [0.8,0.1,...,0.7] (expensive now),
    // 11.49 -> 11.51 -> 11.49 -> 11.51 must not toggle the pump mode; the
    // same holds with a cheap current slot near the upper edge.
    for(const double* prices:{expensive,cheap}) {
        BootstrapInput w{};
        w.indoor_fresh=true;w.desired_room_c=12;
        w.next_total_prices=prices;w.price_count=8;w.prices_contiguous_and_fresh=true;
        const double lower[]={11.49,11.51,11.49,11.51,11.49};
        const double upper[]={12.51,12.49,12.51,12.49,12.51};
        for(const double* seq:{lower,upper}) {
            bool heating=seq==lower?false:true,first=true;
            unsigned toggles=0;
            for(size_t k=0;k<5;++k) {
                w.indoor_c=seq[k];
                const auto r=bootstrap_price_decision(w);
                bool next=heating;
                if(r.action==BootstrapAction::HeatSetpoint) next=true;
                else if(r.action==BootstrapAction::Off) next=false;
                if(!first && next!=heating) ++toggles;
                heating=next;first=false;
            }
            assert(toggles==0);
        }
    }
    in.indoor_c=18.0;
    in.next_total_prices=expensive;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ExpensiveWindow);
    // Manufacturer-unverified minimum mode/command waiting periods removed.
    assert(bootstrap_price_decision(in).action==BootstrapAction::Off);
    in.next_total_prices=cheap;
    in.desired_room_c=24.0;in.indoor_c=15.0;
    d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::HeatSetpoint && near(d.heat_setpoint_c,26.0));
    in.desired_room_c=26.0;in.indoor_c=20.0;
    d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::HeatSetpoint && near(d.heat_setpoint_c,26.0));
    in.desired_room_c=26.5;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.desired_room_c=5.0;in.indoor_c=3.0;
    d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::HeatSetpoint && near(d.heat_setpoint_c,16.0));
    in.desired_room_c=12.0;in.indoor_c=11.8;
    d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::HeatSetpoint && near(d.heat_setpoint_c,16.0));
    in.indoor_c=12.8;
    d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::Off);
    assert(std::strcmp(bootstrap_reason_name(d.reason),"ABOVE_ROOM_TARGET")==0);
    in.price_count=7;
    auto fallback=bootstrap_price_decision(in);
    assert(fallback.action==BootstrapAction::Off);
    assert(fallback.reason==BootstrapReason::ThermostatWithoutPrices);
    assert(!fallback.price_data_valid);
    in.price_count=8;in.prices_contiguous_and_fresh=false;
    fallback=bootstrap_price_decision(in);
    assert(fallback.action==BootstrapAction::Off);
    assert(fallback.reason==BootstrapReason::ThermostatWithoutPrices);
    // The room deadband makes no unrequested command when prices are missing.
    in.indoor_c=12.1;in.desired_room_c=12.0;
    fallback=bootstrap_price_decision(in);
    assert(fallback.action==BootstrapAction::Hold);
    assert(fallback.reason==BootstrapReason::ThermostatWithoutPrices);
    in.prices_contiguous_and_fresh=true;
    in.indoor_c=std::numeric_limits<double>::quiet_NaN();
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.indoor_c=12.0;
    in.desired_room_c=std::numeric_limits<double>::infinity();
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.desired_room_c=18.0;
    const double corrupted[]={0.8,0.1,0.2,0.3,0.4,0.5,std::numeric_limits<double>::quiet_NaN(),0.7};
    in.next_total_prices=corrupted;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ThermostatWithoutPrices);
    in.next_total_prices=cheap;
    in.price_count=97;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ThermostatWithoutPrices);
}
