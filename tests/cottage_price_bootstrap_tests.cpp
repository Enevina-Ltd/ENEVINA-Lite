#include "cottage/price_bootstrap.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace enevina::cottage;
int main() {
    const double prices[]={0.10,0.20,0.30,0.40,0.50,0.60,0.70,0.80};
    BootstrapInput in{};
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.indoor_fresh=true;in.indoor_c=11.85;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
    in.next_total_prices=prices;in.price_count=8;
    in.prices_contiguous_and_fresh=true;
    auto d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::Heat16 && !d.executable);
    assert(d.reason==BootstrapReason::TemperatureLow);
    assert(std::fabs(d.compared_median_eur_per_kwh-0.45)<0.00001);
    in.indoor_c=17.1;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Off);
    in.indoor_c=16.6;in.confirmed_minutes_in_mode=60;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::CheapWindow);
    const double expensive[]={0.80,0.10,0.20,0.30,0.40,0.50,0.60,0.70};
    in.next_total_prices=expensive;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ExpensiveWindow);
    in.confirmed_minutes_in_mode=10;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    in.confirmed_minutes_in_mode=60;in.currently_heating=true;in.indoor_c=16.0;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    in.indoor_c=16.6;in.currently_heating=false;
    in.price_count=7;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
    in.price_count=8;in.prices_contiguous_and_fresh=false;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
    in.prices_contiguous_and_fresh=true;
    in.indoor_c=std::numeric_limits<double>::quiet_NaN();
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.indoor_c=16.6;
    const double corrupted[]={0.8,0.1,0.2,0.3,0.4,0.5,std::numeric_limits<double>::quiet_NaN(),0.7};
    in.next_total_prices=corrupted;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
    in.next_total_prices=prices;
    in.price_count=97;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingPrices);
}
