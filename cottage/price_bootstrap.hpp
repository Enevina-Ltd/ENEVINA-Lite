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
// How the slot at p[0] compares with the median of the window p[0..n). The
// controller and the plan timeline share this ONE rule, so what the dashboard
// shows is what the controller does. cheap: price <= median - margin,
// expensive: price >= median + margin, margin = max(eur_floor, fraction*|median|).
enum class PriceClass : uint8_t { Unknown=0, Neutral=1, Cheap=2, Expensive=3 };
struct PriceClassResult {
    PriceClass cls=PriceClass::Unknown;
    double median=0;
};
inline PriceClassResult classify_price(const double* p,size_t n,double fraction,double eur_floor,
                                       double price_limit_scale=1.0) {
    PriceClassResult r{};
    if(!p || n<8 || n>96 || !std::isfinite(fraction) || fraction<0 || fraction>1 ||
       !std::isfinite(price_limit_scale) || price_limit_scale<1.0 ||
       price_limit_scale>12.0 || !std::isfinite(eur_floor) ||
       eur_floor<0 || eur_floor>price_limit_scale) return r;
    std::array<double,96> sorted{};
    for(size_t i=0;i<n;++i) {
        if(!std::isfinite(p[i]) ||
           p[i]< -price_limit_scale || p[i]>10*price_limit_scale) return r;
        sorted[i]=p[i];
    }
    std::sort(sorted.begin(),sorted.begin()+n);
    const size_t mid=n/2;
    r.median=n%2?sorted[mid]:(sorted[mid-1]+sorted[mid])*0.5;
    const double margin=std::max(eur_floor,fraction*std::fabs(r.median));
    r.cls=p[0]<=r.median-margin?PriceClass::Cheap:
          p[0]>=r.median+margin?PriceClass::Expensive:PriceClass::Neutral;
    return r;
}
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
    // Owner's comfort limits and how much of the band price decisions may use.
    // All 0 = the original behaviour: limits desired -/+ 0.5 C, no extra
    // pre-heat or coasting room. Limits are HARD: price logic never lets a
    // decision run the room past them (heat at/below lower, off at/above
    // upper). Values are clamped so the band always contains desired -/+ 0.5.
    double comfort_lower_c=0;
    double comfort_upper_c=0;
    // A cheap slot may store heat up to desired+preheat_c; an expensive slot
    // may coast down to desired-coast_c. Both are clamped inside the limits.
    double preheat_c=0;
    double coast_c=0;
    // Scale accepted monetary values to the configured currency (EUR=1).
    double price_limit_scale=1.0;
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
        in.price_count>=8 && in.price_count<=96 &&
        std::isfinite(in.price_limit_scale) && in.price_limit_scale>=1.0 &&
        in.price_limit_scale<=12.0;
    for(size_t i=0;price_valid && i<in.price_count;++i) {
        const double v=in.next_total_prices[i];
        if(!std::isfinite(v) || v< -1*in.price_limit_scale || v>10*in.price_limit_scale) {
            price_valid=false;break;
        }
    }
    bool cheap=false,expensive=false;
    if(price_valid && (!std::isfinite(in.min_price_advantage_fraction) ||
       in.min_price_advantage_fraction<0 || in.min_price_advantage_fraction>1 ||
       !std::isfinite(in.min_price_advantage_eur_per_kwh) ||
       in.min_price_advantage_eur_per_kwh<0 ||
       in.min_price_advantage_eur_per_kwh>in.price_limit_scale)) price_valid=false;
    if(price_valid) {
        const PriceClassResult c=classify_price(in.next_total_prices,in.price_count,
            in.min_price_advantage_fraction,in.min_price_advantage_eur_per_kwh,
            in.price_limit_scale);
        if(c.cls==PriceClass::Unknown) price_valid=false;
        else {
            d.compared_median_eur_per_kwh=c.median;
            d.current_total_eur_per_kwh=in.next_total_prices[0];
            d.price_data_valid=true;
            cheap=c.cls==PriceClass::Cheap;
            expensive=c.cls==PriceClass::Expensive;
        }
    }
    const bool band_ok=std::isfinite(in.comfort_lower_c) && std::isfinite(in.comfort_upper_c) &&
        std::isfinite(in.preheat_c) && std::isfinite(in.coast_c) &&
        in.comfort_lower_c>=0 && in.comfort_upper_c>=0 && in.preheat_c>=0 && in.coast_c>=0 &&
        in.comfort_lower_c<=30 && in.comfort_upper_c<=30 && in.preheat_c<=5 && in.coast_c<=10;
    if(!band_ok) return BootstrapDecision{};      // malformed tuning: never guess
    const double narrow_lo=in.desired_room_c-0.5,narrow_hi=in.desired_room_c+0.5;
    // Hard comfort limits (never narrower than the thermostat deadband).
    const double hard_lo=in.comfort_lower_c>0?std::min(in.comfort_lower_c,narrow_lo):narrow_lo;
    const double hard_hi=in.comfort_upper_c>0?std::max(in.comfort_upper_c,narrow_hi):narrow_hi;
    // Price room stays strictly inside the limits so the hard rules still fire.
    const double preheat=std::min(in.preheat_c,std::max(0.0,hard_hi-in.desired_room_c-0.1));
    const double coast=std::min(in.coast_c,std::max(0.0,in.desired_room_c-hard_lo-0.1));
    const bool below=in.indoor_c<=narrow_lo;       // narrow thermostat band
    const bool above=in.indoor_c>=narrow_hi;
    const bool hard_below=in.indoor_c<=hard_lo;
    const bool hard_above=in.indoor_c>=hard_hi;
    if(hard_above) {
        d.action=BootstrapAction::Off;
        d.reason=price_valid?BootstrapReason::TemperatureHigh:
                             BootstrapReason::ThermostatWithoutPrices;
    } else if(hard_below) {
        d.action=BootstrapAction::HeatSetpoint;
        d.reason=price_valid?BootstrapReason::TemperatureLow:
                             BootstrapReason::ThermostatWithoutPrices;
    } else if(!price_valid) {
        // Without prices the room is a plain thermostat on the narrow band
        // around the target; the wider limits are only for price decisions.
        d.action=above?BootstrapAction::Off:below?BootstrapAction::HeatSetpoint:
                       BootstrapAction::Hold;
        d.reason=BootstrapReason::ThermostatWithoutPrices;
    } else if(cheap && (in.comfort_lower_c>0 || in.comfort_upper_c>0 ||
                        in.preheat_c>0 || in.coast_c>0)) {
        // A configured profile has an upper storage allowance. HOLD at its
        // ceiling would let a previously running pump exceed that allowance.
        // The 0.1 C decision gap prevents 0.02 C sensor jitter from toggling
        // commands without extending the owner's chosen temperature range.
        const double ceiling=in.desired_room_c+preheat;
        d.action=in.indoor_c>=ceiling?BootstrapAction::Off:
                 in.indoor_c<=ceiling-0.1?BootstrapAction::HeatSetpoint:
                 BootstrapAction::Hold;
        d.reason=d.action==BootstrapAction::Hold?
            BootstrapReason::DeadbandHold:BootstrapReason::CheapWindow;
    } else if(expensive && (in.comfort_lower_c>0 || in.comfort_upper_c>0 ||
                            in.preheat_c>0 || in.coast_c>0)) {
        // Coasting ends at target-coast, not at the HARD outer comfort limit.
        // Request heat below the profile floor so OFF cannot linger and drain
        // extra thermal energy during expensive periods.
        const double floor=in.desired_room_c-coast;
        d.action=in.indoor_c<=floor?BootstrapAction::HeatSetpoint:
                 in.indoor_c>=floor+0.1?BootstrapAction::Off:
                 BootstrapAction::Hold;
        d.reason=d.action==BootstrapAction::Hold?
            BootstrapReason::DeadbandHold:BootstrapReason::ExpensiveWindow;
    } else if(cheap && in.indoor_c<in.desired_room_c+preheat) {
        // Pre-heat only in the LOWER half of the deadband ...
        d.action=BootstrapAction::HeatSetpoint;
        d.reason=BootstrapReason::CheapWindow;
    } else if(expensive && in.indoor_c>=in.desired_room_c-coast) {
        // ... and coast only in the UPPER half. A HEAT<->OFF change then
        // needs >= 0.5 C of real temperature movement or a price-signal
        // change, never a 0.02 C sensor wobble at a deadband edge
        // (Codex P1, PR #28).
        d.action=BootstrapAction::Off;
        d.reason=BootstrapReason::ExpensiveWindow;
    } else if(!cheap && !expensive) {
        // No material price difference: behave like a thermostat on the
        // narrow band (stored heat above the target is simply not topped up).
        d.action=above?BootstrapAction::Off:below?BootstrapAction::HeatSetpoint:
                       BootstrapAction::Hold;
        d.reason=BootstrapReason::NeutralPriceWindow;
    } else {
        d.action=BootstrapAction::Hold;
        d.reason=BootstrapReason::DeadbandHold;
    }
    if(d.action==BootstrapAction::HeatSetpoint) {
        // Device HEAT setpoint is not the room target below 16C.
        double lift=price_valid?(cheap?(below?2.0:1.0):0.5):0.5;
        if(price_valid && cheap && preheat>0) lift=std::max(lift,preheat+0.5);
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
