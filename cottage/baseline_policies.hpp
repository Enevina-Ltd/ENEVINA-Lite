#pragma once
// Reference control policies for the HEAT 16 C / OFF cottage model.
//
// These are COMPARISON baselines for the optimization audit, not new control
// paths. They emit no device commands. Each closed-loop simulation uses the
// same physics::advance() and the same total-price slots as the MPC, and
// honours the same minimum HEAT/OFF dwell times as the MPC and the MILP.
//
//  * Thermostat: ordinary hysteresis thermostat around a fixed room target.
//  * PriceThreshold: the EXISTING firmware price bootstrap (PR #22,
//    bootstrap_price_decision) applied to the same room target. HEAT_SETPOINT
//    is mapped to HEAT 16 C, OFF to OFF, HOLD keeps the current mode.
//
// Terminal fairness: an optimizer must not "save" by ending colder. Both
// baselines get the same end-of-horizon obligation as the MPC/MILP: during
// the last `terminal_guard_slots`, they heat whenever the room is below the
// initial temperature (+ margin) and the dwell time allows it. Any remaining
// deficit is reported by the caller, never hidden.
#include "cottage/physics.hpp"
#include "cottage/price_bootstrap.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace enevina::cottage {
enum class BaselinePolicy : uint8_t { Thermostat, PriceThreshold };

struct BaselineOptions {
    double target_c=10.0;          // room target held by the baseline
    double hysteresis_c=0.25;      // thermostat half band
    double safety_margin_c=0.6;    // forced HEAT when an OFF slot would cross min+margin
    size_t terminal_guard_slots=8; // equal-terminal obligation window (2 h)
    double terminal_margin_c=0.15;
};

struct BaselineResult {
    bool ok=false;                 // false: invalid input only
    size_t count=0;
    double cost_eur=0;
    double electric_kwh=0;
    double thermal_kwh=0;
    double minimum_c=0;
    double maximum_c=0;
    double final_c=0;
    unsigned starts=0;
    unsigned dwell_overrides=0;    // always 0: dwell is never overridden
    unsigned limit_violations=0;   // slots outside [planning_min, maximum]
    std::array<uint8_t,192> heat_16_c{};
};

namespace detail {
inline bool dwell_allows(const Parameters& p, const State& s, bool next_on) {
    if(next_on==s.heat_requested) return true;
    const unsigned required=s.heat_requested?p.min_heat_minutes:p.min_off_minutes;
    return s.minutes_in_mode>=required;
}
} // namespace detail

inline BaselineResult simulate_baseline(BaselinePolicy policy,const Parameters& p,
    const State& initial,const Period* periods,size_t count,
    const BaselineOptions& o={}) {
    BaselineResult r{};
    if(!valid(p)||!periods||count==0||count>192||!std::isfinite(initial.indoor_c))
        return r;
    for(size_t i=0;i<count;++i) if(!valid(p,initial,periods[i])) return r;
    const double min_c=p.planning_min_c+p.planning_uncertainty_c;
    State s=initial;
    r.minimum_c=r.maximum_c=s.indoor_c;
    for(size_t t=0;t<count;++t) {
        bool want=s.heat_requested;
        if(policy==BaselinePolicy::Thermostat) {
            if(s.indoor_c<=o.target_c-o.hysteresis_c) want=true;
            else if(s.indoor_c>=o.target_c+o.hysteresis_c) want=false;
        } else {
            // Next eight 15-minute TOTAL prices, exactly like the firmware.
            constexpr size_t N=8;
            std::array<double,N> next{};
            const bool full=t+N<=count;
            for(size_t k=0;full && k<N;++k) next[k]=periods[t+k].total_price_eur_per_kwh;
            BootstrapInput in{};
            in.indoor_c=s.indoor_c;
            in.indoor_fresh=true;
            in.desired_room_c=o.target_c;
            in.next_total_prices=next.data();
            in.price_count=N;
            // Near the horizon end the firmware would have the next day's
            // prices; here it degrades to its no-price thermostat mode.
            in.prices_contiguous_and_fresh=full;
            const auto d=bootstrap_price_decision(in);
            if(d.action==BootstrapAction::HeatSetpoint) want=true;
            else if(d.action==BootstrapAction::Off) want=false;
            // Hold / Unavailable: keep current mode.
        }
        // Comfort-floor guard: if staying OFF this slot would end below
        // min+margin, request HEAT (subject to dwell, never overridden).
        if(!want) {
            const auto idle=advance(p,s,periods[t],false);
            if(idle.next.indoor_c<min_c+o.safety_margin_c) want=true;
        }
        if(count-t<=o.terminal_guard_slots &&
           s.indoor_c<initial.indoor_c+o.terminal_margin_c) want=true;
        if(want && s.indoor_c>=p.maximum_indoor_c-0.05) want=false;
        if(!detail::dwell_allows(p,s,want)) want=s.heat_requested;
        if(want && !s.heat_requested) r.starts++;
        const auto step=advance(p,s,periods[t],want);
        r.heat_16_c[t]=want?1:0;
        r.cost_eur+=step.cost_eur;
        r.electric_kwh+=step.electric_kwh;
        r.thermal_kwh+=step.delivered_thermal_kw*slot_hours;
        s=step.next;
        r.minimum_c=std::min(r.minimum_c,s.indoor_c);
        r.maximum_c=std::max(r.maximum_c,s.indoor_c);
        if(!std::isfinite(s.indoor_c) || s.indoor_c<min_c-1e-8 ||
           s.indoor_c>p.maximum_indoor_c+1e-8) r.limit_violations++;
    }
    r.ok=true;
    r.count=count;
    r.final_c=s.indoor_c;
    return r;
}
} // namespace enevina::cottage
