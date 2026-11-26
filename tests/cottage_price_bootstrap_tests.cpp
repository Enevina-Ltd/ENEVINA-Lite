#include "cottage/price_bootstrap.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <cstring>
using namespace enevina::cottage;
static bool near(double a,double b){return std::fabs(a-b)<1e-8;}
int main() {
    {
        BootstrapInput generic{};
        generic.indoor_fresh=true;generic.indoor_c=8;generic.desired_room_c=10;
        assert(bootstrap_price_decision(generic).reason==BootstrapReason::PumpCapabilitiesUnknown);
        for(const enevina::PumpHeatingLimits limits:{
            enevina::PumpHeatingLimits{8,30,1,true,true},
            enevina::PumpHeatingLimits{10,28,0.5,true,true},
            enevina::PumpHeatingLimits{18,30,1,true,true},
            enevina::PumpHeatingLimits{20,32,2,true,true}}) {
            generic.heating=limits;
            auto d=bootstrap_price_decision(generic);
            assert(d.action==BootstrapAction::HeatSetpoint);
            assert(limits.accepts(d.heat_setpoint_c));
            assert(d.heat_setpoint_c==limits.quantized(10.5));
        }
        generic.heating={8,30,1,true,false};generic.indoor_c=15;
        assert(bootstrap_price_decision(generic).action==BootstrapAction::Unavailable);
        generic.heating={8,30,1,true,true};
        assert(bootstrap_price_decision(generic).action==BootstrapAction::Off);
        generic.indoor_c=8;generic.heating.step_c=0;
        assert(bootstrap_price_decision(generic).action==BootstrapAction::Unavailable);
    }
    const double cheap[]={0.10,0.20,0.30,0.40,0.50,0.60,0.70,0.80};
    const double expensive[]={0.80,0.10,0.20,0.30,0.40,0.50,0.60,0.70};
    {
        // Below-minimum CN105 room thermostat: do not start on a tiny
        // price-induced temperature fluctuation, and end at the room target.
        BootstrapInput low{};low.heating={16,26,0.5,true,true};
        low.indoor_fresh=true;low.desired_room_c=12;
        low.low_temperature_cycle=true;
        const double neutral[]={.2,.2,.2,.2,.2,.2,.2,.2};
        for(const double* prices:{static_cast<const double*>(nullptr),neutral,cheap,expensive}) {
            low.next_total_prices=prices;low.price_count=prices?8:0;
            low.prices_contiguous_and_fresh=prices!=nullptr;
            low.indoor_c=11.51;
            assert(bootstrap_price_decision(low).action==BootstrapAction::Hold);
            low.indoor_c=11.50;
            const auto heat=bootstrap_price_decision(low);
            assert(heat.action==BootstrapAction::HeatSetpoint && near(heat.heat_setpoint_c,16));
            low.indoor_c=11.99;
            assert(bootstrap_price_decision(low).action==BootstrapAction::Hold);
            low.indoor_c=12.0;
            assert(low_temperature_stop(bootstrap_price_decision(low)));
            low.indoor_c=16.0;
            assert(low_temperature_stop(bootstrap_price_decision(low)));
        }
        // Price flexibility only inside configured room limits. Cheap
        // preheat does not silently create a 16 C or 30-minute room target.
        low.comfort_lower_c=11;low.comfort_upper_c=13;
        low.preheat_c=0.4;low.coast_c=0.4;
        low.next_total_prices=cheap;low.indoor_c=11.49;
        assert(bootstrap_price_decision(low).action==BootstrapAction::HeatSetpoint);
        low.indoor_c=11.89;assert(bootstrap_price_decision(low).action==BootstrapAction::Hold);
        low.indoor_c=12.39;assert(bootstrap_price_decision(low).action==BootstrapAction::Hold);
        low.indoor_c=12.41;assert(low_temperature_stop(bootstrap_price_decision(low)));
        low.next_total_prices=expensive;low.indoor_c=11.11;
        assert(bootstrap_price_decision(low).action==BootstrapAction::Hold);
        low.indoor_c=11.09;assert(bootstrap_price_decision(low).action==BootstrapAction::HeatSetpoint);
        low.indoor_c=11.61;
        const auto price_stop=bootstrap_price_decision(low);
        assert(price_stop.action==BootstrapAction::Off);
        assert(!low_temperature_stop(price_stop));
        low.indoor_c=12;assert(low_temperature_stop(bootstrap_price_decision(low)));
        // Wrong mode/target or stale/malformed measurements cannot issue an
        // exceptional stop. Normal thermostat behavior remains unchanged.
        low.indoor_fresh=false;
        assert(!low_temperature_stop(bootstrap_price_decision(low)));
        low.indoor_fresh=true;low.indoor_c=std::numeric_limits<double>::quiet_NaN();
        assert(!low_temperature_stop(bootstrap_price_decision(low)));
        low.indoor_c=16.5;low.desired_room_c=16;
        assert(!low_temperature_stop(bootstrap_price_decision(low)));
        low.desired_room_c=12;low.indoor_c=12.5;low.low_temperature_cycle=false;
        assert(!low_temperature_stop(bootstrap_price_decision(low)));
    }

    BootstrapInput in{};in.heating={16,26,0.5,true,true};
    assert(bootstrap_price_decision(in).reason==BootstrapReason::MissingTemperature);
    in.indoor_fresh=true;in.indoor_c=15.0;
    auto no_prices=bootstrap_price_decision(in);
    assert(no_prices.action==BootstrapAction::HeatSetpoint);
    assert(no_prices.reason==BootstrapReason::ThermostatWithoutPrices);
    assert(!no_prices.price_data_valid);
    assert(near(no_prices.heat_setpoint_c,16.5)); // default target is 16C
    // Swedish/Norwegian amounts are in major local currency units/kWh.
    // They must not be rejected using the old EUR-only [-1,10] guard.
    const double sek_prices[]={-1.5,-1.2,1.2,1.3,1.4,1.5,1.6,1.7};
    BootstrapInput sek{};sek.heating={16,26,0.5,true,true};
    sek.indoor_fresh=true;sek.indoor_c=12;sek.desired_room_c=13;
    sek.next_total_prices=sek_prices;sek.price_count=8;sek.prices_contiguous_and_fresh=true;
    sek.price_limit_scale=12;sek.min_price_advantage_eur_per_kwh=0.06;
    assert(bootstrap_price_decision(sek).price_data_valid);
    sek.price_limit_scale=1;
    assert(!bootstrap_price_decision(sek).price_data_valid);
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
        BootstrapInput w{};w.heating={16,26,0.5,true,true};
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
    // Regression (audit 18.11.2026): flat prices are NOT a pre-heat signal.
    // Old rule "current <= median" heated in every flat slot and lost to a
    // plain thermostat in the flat_price audit case.
    const double flat[]={0.12,0.12,0.12,0.12,0.12,0.12,0.12,0.12};
    in.next_total_prices=flat;in.desired_room_c=18.0;in.indoor_c=18.0;
    d=bootstrap_price_decision(in);
    assert(d.action==BootstrapAction::Hold);
    assert(d.reason==BootstrapReason::NeutralPriceWindow);
    assert(std::strcmp(bootstrap_reason_name(d.reason),"NEUTRAL_COST_WINDOW")==0);
    // Below the advantage margin (median 0.12, margin 0.012): still neutral.
    const double slight[]={0.111,0.12,0.12,0.12,0.12,0.12,0.12,0.12};
    in.next_total_prices=slight;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    const double clear[]={0.100,0.12,0.12,0.12,0.12,0.12,0.12,0.12};
    // Split deadband: a clear price advantage pre-heats only below target,
    // a clear premium coasts only at/above target (otherwise HOLD).
    in.next_total_prices=clear;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::DeadbandHold);
    in.indoor_c=17.9;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::CheapWindow);
    in.indoor_c=18.0;
    const double peak[]={0.140,0.12,0.12,0.12,0.12,0.12,0.12,0.12};
    in.next_total_prices=peak;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ExpensiveWindow);
    in.indoor_c=17.9;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::DeadbandHold);
    in.indoor_c=18.0;
    // Flat prices still heat below / stop above the room deadband.
    in.next_total_prices=flat;in.indoor_c=17.4;
    assert(bootstrap_price_decision(in).action==BootstrapAction::HeatSetpoint);
    in.indoor_c=18.6;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Off);
    // Invalid advantage configuration degrades to the no-price thermostat.
    in.indoor_c=18.0;in.min_price_advantage_fraction=-1;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ThermostatWithoutPrices);
    in.min_price_advantage_fraction=0.10;
    in.next_total_prices=cheap;
    in.price_count=97;
    assert(bootstrap_price_decision(in).reason==BootstrapReason::ThermostatWithoutPrices);
}
