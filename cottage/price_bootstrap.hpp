#pragma once
// Deterministic, manufacturer-independent price-aware setpoint fallback before
// building physics has enough observations for calibrated MPC.
// This is a proposal only; PumpAdapter/Supervisor must separately authorize writes.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace enevina::cottage {
enum class BootstrapAction : uint8_t { Unavailable, HeatSetpoint, Off, Hold };
enum class BootstrapReason : uint8_t {
    MissingTemperature, MissingPrices, TemperatureLow, TemperatureHigh,
    CheapWindow, ExpensiveWindow, Hysteresis
};
struct BootstrapInput {
    double indoor_c=0;
    bool indoor_fresh=false;
    // User-selected room temperature, not necessarily the pump sensor temperature.
    // Can be lower than the device's 16C minimum: heating then cycles by room
    // temperature instead of trying an unsupported device setpoint.
    double desired_room_c=16.0;
    // Prices include ALL marginal EUR/kWh components for each 15m slot.
    const double* next_total_prices=nullptr;
    size_t price_count=0;
    // True only for consecutive, current-to-future UTC 15-minute slots.
    bool prices_contiguous_and_fresh=false;
    bool currently_heating=false;
    // Based on confirmed actuator transitions; never inferred from SSE heartbeat.
    unsigned confirmed_minutes_in_mode=0;
    // A real, acknowledged actuator transition is necessary for dwell
    // enforcement. Display-only proposals cannot invent pump mode uptime.
    bool actuator_dwell_verified=false;
};
struct BootstrapDecision {
    BootstrapAction action=BootstrapAction::Unavailable;
    BootstrapReason reason=BootstrapReason::MissingTemperature;
    double compared_median_eur_per_kwh=0;
    double current_total_eur_per_kwh=0;
    double requested_room_c=0;
    double heat_setpoint_c=0; // only valid for HeatSetpoint, 16..26 in 0.5C steps
    // Physical commands NEVER originate in this model-free module.
    bool executable=false;
};
inline BootstrapDecision bootstrap_price_decision(const BootstrapInput& in) {
    BootstrapDecision d{};
    if(!in.indoor_fresh || !std::isfinite(in.indoor_c) ||
        in.indoor_c< -10 || in.indoor_c>40 ||
        !std::isfinite(in.desired_room_c) ||
        in.desired_room_c<5.0 || in.desired_room_c>26.0) return d;
    d.requested_room_c=in.desired_room_c;
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
    // Room-target hysteresis is independent of the CN105 pump setpoint.
    // The latter is only a suggested actuator request, not a guaranteed
    // resulting cabin temperature (that requires a calibrated model).
    const bool cheap=in.next_total_prices[0]<=d.compared_median_eur_per_kwh;
    const bool below=in.indoor_c<=in.desired_room_c-0.5;
    const bool above=in.indoor_c>=in.desired_room_c+0.5;
    if(above) {
        d.action=BootstrapAction::Off;
        d.reason=BootstrapReason::TemperatureHigh;
    } else if(below) {
        d.action=BootstrapAction::HeatSetpoint;
        d.reason=BootstrapReason::TemperatureLow;
    } else if(in.actuator_dwell_verified &&
       ((in.currently_heating && in.indoor_c<in.desired_room_c+0.25) ||
        in.confirmed_minutes_in_mode<60)) {
        d.action=BootstrapAction::Hold;
        d.reason=BootstrapReason::Hysteresis;
    } else if(cheap) {
        d.action=BootstrapAction::HeatSetpoint;
        d.reason=BootstrapReason::CheapWindow;
    } else {
        d.action=BootstrapAction::Off;
        d.reason=BootstrapReason::ExpensiveWindow;
    }
    if(d.action==BootstrapAction::HeatSetpoint) {
        // Nominal target+0.5C, cheap-hours boost +2C when below target or
        // +1C in the deadband. Conservative heuristic, NOT COP optimization.
        const double lift=cheap?(below?2.0:1.0):0.5;
        const double raw=std::max(16.0,std::min(26.0,in.desired_room_c+lift));
        d.heat_setpoint_c=std::round(raw*2.0)/2.0;
    }
    return d;
}
inline const char* bootstrap_reason_name(BootstrapReason reason) {
    switch(reason) {
    case BootstrapReason::MissingTemperature:return "RUUVI_STALE";
    case BootstrapReason::MissingPrices:return "PRICES_UNAVAILABLE";
    case BootstrapReason::TemperatureLow:return "BELOW_ROOM_TARGET";
    case BootstrapReason::TemperatureHigh:return "ABOVE_ROOM_TARGET";
    case BootstrapReason::CheapWindow:return "LOW_COST_WINDOW";
    case BootstrapReason::ExpensiveWindow:return "HIGH_COST_WINDOW";
    case BootstrapReason::Hysteresis:return "MIN_RUN_AND_HYSTERESIS";
    }
    return "UNKNOWN";
}
inline const char* bootstrap_action_name(BootstrapAction action) {
    switch(action) {
    case BootstrapAction::Unavailable:return "UNAVAILABLE";
    case BootstrapAction::HeatSetpoint:return "HEAT_SETPOINT";
    case BootstrapAction::Off:return "OFF";
    case BootstrapAction::Hold:return "HOLD";
    }
    return "UNAVAILABLE";
}
}
