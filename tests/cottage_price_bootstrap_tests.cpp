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
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
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
    in.next_total_prices=cheap;
    d=bootstrap_price_decision(in);
    assert(d.reason==BootstrapReason::CheapWindow);
    assert(near(d.heat_setpoint_c,19.0));
    in.next_total_prices=expensive;
    in.confirmed_minutes_in_mode=10;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ExpensiveWindow);
    in.actuator_dwell_verified=true;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    in.confirmed_minutes_in_mode=60;in.currently_heating=true;in.indoor_c=18.0;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    in.currently_heating=false;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Off);
    in.actuator_dwell_verified=false;
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
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
    in.price_count=8;in.prices_contiguous_and_fresh=false;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
    in.prices_contiguous_and_fresh=true;
    in.indoor_c=std::numeric_limits<double>::quiet_NaN();
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.indoor_c=12.0;
    in.desired_room_c=std::numeric_limits<double>::infinity();
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.desired_room_c=18.0;
    const double corrupted[]={0.8,0.1,0.2,0.3,0.4,0.5,std::numeric_limits<double>::quiet_NaN(),0.7};
    in.next_total_prices=corrupted;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
    in.next_total_prices=cheap;
    in.price_count=97;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
}
