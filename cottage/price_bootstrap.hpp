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
    MissingTemperature, MissingPrices, NoHeatingRequested, TemperatureLow, TemperatureHigh,
    CheapWindow, ExpensiveWindow, ThermostatWithoutPrices, NeutralPriceWindow,
    DeadbandHold
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
    // A slot is "cheap" (pre-heat) or "expensive" (coast) only when it differs
    // from the 2 h median by at least this fraction of |median| and by at least
    // min_price_advantage_eur_per_kwh. Pre-heating at a price equal to the
    // median only adds heat loss. Audit: docs/OPTIMIZATION_AUDIT_20261118.md.
    double min_price_advantage_fraction=0.10;
    double min_price_advantage_eur_per_kwh=0.005;
};
struct BootstrapDecision {
    BootstrapAction action=BootstrapAction::Unavailable;
    BootstrapReason reason=BootstrapReason::MissingTemperature;
    double compared_median_eur_per_kwh=0;
    double current_total_eur_per_kwh=0;
    bool price_data_valid=false;
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
    // Price data is OPTIONAL for maintaining a user-selected temperature.
    // A Nord Pool/network outage must not disable the local thermostat.
    bool price_valid=in.prices_contiguous_and_fresh && in.next_total_prices &&
        in.price_count>=8 && in.price_count<=96;
    std::array<double,96> sorted{};
    for(size_t i=0;price_valid && i<in.price_count;++i) {
        const double v=in.next_total_prices[i];
        if(!std::isfinite(v) || v< -1 || v>10) {
            price_valid=false;break;
        }
        sorted[i]=v;
    }
    bool cheap=false,expensive=false;
    if(price_valid && (!std::isfinite(in.min_price_advantage_fraction) ||
       in.min_price_advantage_fraction<0 || in.min_price_advantage_fraction>1 ||
       !std::isfinite(in.min_price_advantage_eur_per_kwh) ||
       in.min_price_advantage_eur_per_kwh<0 ||
       in.min_price_advantage_eur_per_kwh>1)) price_valid=false;
    if(price_valid) {
        std::sort(sorted.begin(),sorted.begin()+in.price_count);
        const size_t mid=in.price_count/2;
        d.compared_median_eur_per_kwh=in.price_count%2?
            sorted[mid]:(sorted[mid-1]+sorted[mid])*0.5;
        d.current_total_eur_per_kwh=in.next_total_prices[0];
        d.price_data_valid=true;
        const double margin=std::max(in.min_price_advantage_eur_per_kwh,
            in.min_price_advantage_fraction*std::fabs(d.compared_median_eur_per_kwh));
        cheap=in.next_total_prices[0]<=d.compared_median_eur_per_kwh-margin;
        expensive=in.next_total_prices[0]>=d.compared_median_eur_per_kwh+margin;
    }
    const bool below=in.indoor_c<=in.desired_room_c-0.5;
    const bool above=in.indoor_c>=in.desired_room_c+0.5;
    if(above) {
        d.action=BootstrapAction::Off;
        d.reason=price_valid?BootstrapReason::TemperatureHigh:
                             BootstrapReason::ThermostatWithoutPrices;
    } else if(below) {
        d.action=BootstrapAction::HeatSetpoint;
        d.reason=price_valid?BootstrapReason::TemperatureLow:
                             BootstrapReason::ThermostatWithoutPrices;
    } else if(!price_valid) {
        // Already within target deadband: keep pump's current mode.
        d.action=BootstrapAction::Hold;
        d.reason=BootstrapReason::ThermostatWithoutPrices;
    } else if(cheap && in.indoor_c<in.desired_room_c) {
        // Pre-heat only in the LOWER half of the deadband ...
        d.action=BootstrapAction::HeatSetpoint;
        d.reason=BootstrapReason::CheapWindow;
    } else if(expensive && in.indoor_c>=in.desired_room_c) {
        // ... and coast only in the UPPER half. A HEAT<->OFF change then
        // needs >= 0.5 C of real temperature movement or a price-signal
        // change, never a 0.02 C sensor wobble at a deadband edge
        // (Codex P1, PR #28).
        d.action=BootstrapAction::Off;
        d.reason=BootstrapReason::ExpensiveWindow;
    } else if(!cheap && !expensive) {
        // No material price difference: behave like a thermostat.
        d.action=BootstrapAction::Hold;
        d.reason=BootstrapReason::NeutralPriceWindow;
    } else {
        d.action=BootstrapAction::Hold;
        d.reason=BootstrapReason::DeadbandHold;
    }
    if(d.action==BootstrapAction::HeatSetpoint) {
        // Device HEAT setpoint is not the room target below 16C.
        const double lift=price_valid?(cheap?(below?2.0:1.0):0.5):0.5;
        const double raw=std::max(16.0,std::min(26.0,in.desired_room_c+lift));
        d.heat_setpoint_c=std::round(raw*2.0)/2.0;
    }
    return d;
}
inline const char* bootstrap_reason_name(BootstrapReason reason) {
    switch(reason) {
    case BootstrapReason::MissingTemperature:return "RUUVI_STALE";
    case BootstrapReason::MissingPrices:return "PRICES_UNAVAILABLE";
    case BootstrapReason::NoHeatingRequested:return "NO_HEATING_REQUESTED";
    case BootstrapReason::TemperatureLow:return "BELOW_ROOM_TARGET";
    case BootstrapReason::TemperatureHigh:return "ABOVE_ROOM_TARGET";
    case BootstrapReason::CheapWindow:return "LOW_COST_WINDOW";
    case BootstrapReason::ExpensiveWindow:return "HIGH_COST_WINDOW";
    case BootstrapReason::ThermostatWithoutPrices:return "THERMOSTAT_NO_PRICES";
    case BootstrapReason::NeutralPriceWindow:return "NEUTRAL_COST_WINDOW";
    case BootstrapReason::DeadbandHold:return "HOLD_IN_DEADBAND";
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
