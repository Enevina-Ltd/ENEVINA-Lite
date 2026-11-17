#pragma once
// Deterministic, manufacturer-independent price-aware 16C fallback before
// building physics has enough observations for calibrated MPC.
// This is a proposal only; PumpAdapter/Supervisor must separately authorize writes.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace enevina::cottage {
enum class BootstrapAction : uint8_t { Unavailable, Heat16, Off, Hold };
enum class BootstrapReason : uint8_t {
    MissingTemperature, MissingPrices, TemperatureLow, TemperatureHigh,
    CheapWindow, ExpensiveWindow, Hysteresis
};
struct BootstrapInput {
    double indoor_c=0;
    bool indoor_fresh=false;
    // Prices include ALL marginal EUR/kWh components for each 15m slot.
    const double* next_total_prices=nullptr;
    size_t price_count=0;
    // True only for consecutive, current-to-future UTC 15-minute slots.
    bool prices_contiguous_and_fresh=false;
    bool currently_heating=false;
    // Based on confirmed actuator transitions; never inferred from SSE heartbeat.
    unsigned confirmed_minutes_in_mode=0;
};
struct BootstrapDecision {
    BootstrapAction action=BootstrapAction::Unavailable;
    BootstrapReason reason=BootstrapReason::MissingTemperature;
    double compared_median_eur_per_kwh=0;
    double current_total_eur_per_kwh=0;
    // Physical commands NEVER originate in this model-free module.
    bool executable=false;
};
inline BootstrapDecision bootstrap_price_decision(const BootstrapInput& in) {
    BootstrapDecision d{};
    if(!in.indoor_fresh || !std::isfinite(in.indoor_c) ||
        in.indoor_c< -10 || in.indoor_c>40) return d;
    if(!in.prices_contiguous_and_fresh || !in.next_total_prices ||
       in.price_count<8 || in.price_count>96) {
        d.reason=BootstrapReason::MissingPrices;
        return d;
    }
    // Bounded sort, no allocation and no library/platform dependence.
    std::array<double,96> sorted{};
    for(size_t i=0;i<in.price_count;i++) {
        const double v=in.next_total_prices[i];
        if(!std::isfinite(v) || v< -1 || v>10) {
            d.reason=BootstrapReason::MissingPrices;return d;
        }
        sorted[i]=v;
    }
    std::sort(sorted.begin(),sorted.begin()+in.price_count);
    const size_t mid=in.price_count/2;
    d.compared_median_eur_per_kwh=in.price_count%2?
        sorted[mid]:(sorted[mid-1]+sorted[mid])*0.5;
    d.current_total_eur_per_kwh=in.next_total_prices[0];
    // Ruuvi-based hard temperature band; never rely on an uncalibrated
    // heat-loss / COP curve. 16C is a HEAT setpoint, not inferred room target.
    if(in.indoor_c<=15.5) {
        d.action=BootstrapAction::Heat16;
        d.reason=BootstrapReason::TemperatureLow;
        return d;
    }
    if(in.indoor_c>=17.0) {
        d.action=BootstrapAction::Off;
        d.reason=BootstrapReason::TemperatureHigh;
        return d;
    }
    // In the comfort band the cheapest half of the verified 2h horizon
    // is used for additional heating, without fabricating a COP saving.
    // Hysteresis: do not flip a running compressor inside the 16C band
    // if the temperature has not reached 16.5C.
    if(in.currently_heating && in.indoor_c<16.5) {
        d.action=BootstrapAction::Hold;
        d.reason=BootstrapReason::Hysteresis;
    } else if(in.confirmed_minutes_in_mode<60) {
        d.action=BootstrapAction::Hold;
        d.reason=BootstrapReason::Hysteresis;
    } else if(in.next_total_prices[0] <= d.compared_median_eur_per_kwh) {
        d.action=BootstrapAction::Heat16;
        d.reason=BootstrapReason::CheapWindow;
    } else {
        d.action=BootstrapAction::Off;
        d.reason=BootstrapReason::ExpensiveWindow;
    }
    return d;
}
inline const char* bootstrap_reason_name(BootstrapReason reason) {
    switch(reason) {
    case BootstrapReason::MissingTemperature:return "RUUVI_STALE";
    case BootstrapReason::MissingPrices:return "PRICES_UNAVAILABLE";
    case BootstrapReason::TemperatureLow:return "TEMPERATURE_BELOW_15_5";
    case BootstrapReason::TemperatureHigh:return "TEMPERATURE_ABOVE_17";
    case BootstrapReason::CheapWindow:return "LOW_COST_WINDOW";
    case BootstrapReason::ExpensiveWindow:return "HIGH_COST_WINDOW";
    case BootstrapReason::Hysteresis:return "MIN_RUN_AND_HYSTERESIS";
    }
    return "UNKNOWN";
}
inline const char* bootstrap_action_name(BootstrapAction action) {
    switch(action) {
    case BootstrapAction::Unavailable:return "UNAVAILABLE";
    case BootstrapAction::Heat16:return "HEAT_16";
    case BootstrapAction::Off:return "OFF";
    case BootstrapAction::Hold:return "HOLD";
    }
    return "UNAVAILABLE";
}
}
