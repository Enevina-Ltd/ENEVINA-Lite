#include "cottage/physics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace enevina::cottage {
namespace {
bool finite(double x) { return std::isfinite(x); }
}
bool valid(const Parameters& p) {
    return finite(p.thermal_mass_kwh_per_c) && p.thermal_mass_kwh_per_c > 0 &&
        finite(p.loss_kw_per_c) && p.loss_kw_per_c > 0 &&
        finite(p.heat_kw_at_zero_c) && p.heat_kw_at_zero_c > 0 &&
        finite(p.heat_kw_change_per_outdoor_c) &&
        finite(p.minimum_heat_kw) && p.minimum_heat_kw > 0 &&
        p.minimum_heat_kw <= p.heat_kw_at_zero_c &&
        finite(p.cop_at_zero_c) && p.cop_at_zero_c > 0 &&
        finite(p.cop_change_per_outdoor_c) &&
        finite(p.minimum_cop) && p.minimum_cop >= 1 &&
        finite(p.maximum_cop) && p.maximum_cop >= p.minimum_cop &&
        finite(p.heat_standby_kw) && p.heat_standby_kw >= 0 &&
        finite(p.off_standby_kw) && p.off_standby_kw >= 0 &&
        finite(p.startup_electric_kwh) && p.startup_electric_kwh >= 0 &&
        finite(p.startup_delay_minutes) && p.startup_delay_minutes >= 0 &&
        p.startup_delay_minutes < slot_minutes &&
        finite(p.planning_min_c) && finite(p.critical_c) &&
        p.planning_min_c >= p.critical_c && p.critical_c >= -10 &&
        finite(p.planning_uncertainty_c) && p.planning_uncertainty_c >= 0 &&
        p.planning_uncertainty_c <= 3.0 &&
        finite(p.maximum_indoor_c) &&
        p.maximum_indoor_c > p.planning_min_c + p.planning_uncertainty_c &&
        p.maximum_indoor_c <= 16.0 && // Only HEAT 16 C / OFF permitted.
        finite(p.switching_penalty_eur) && p.switching_penalty_eur >= 0 &&
        p.min_heat_minutes >= slot_minutes && p.min_off_minutes >= slot_minutes &&
        p.min_heat_minutes <= 480 && p.min_off_minutes <= 480 &&
        p.min_heat_minutes % slot_minutes == 0 &&
        p.min_off_minutes % slot_minutes == 0 &&
        finite(p.currency_limit_scale) && p.currency_limit_scale >= 1 &&
        p.currency_limit_scale <= 12 &&
        finite(p.stop_afterheat_kwh) && p.stop_afterheat_kwh >= 0 &&
        p.stop_afterheat_kwh <= 2.0;
}
bool valid(const Parameters& p, const State& s, const Period& d) {
    return valid(p) && finite(s.indoor_c) &&
        s.indoor_c >= p.critical_c - 10 && s.indoor_c <= 40 &&
        finite(d.outdoor_c) && d.outdoor_c >= -55 && d.outdoor_c <= 45 &&
        finite(d.total_price_eur_per_kwh) &&
        d.total_price_eur_per_kwh >= -5*p.currency_limit_scale &&
        d.total_price_eur_per_kwh <= 10*p.currency_limit_scale &&
        finite(d.internal_gain_kw) && std::abs(d.internal_gain_kw) < 30 &&
        finite(d.defrost_capacity_factor) &&
        d.defrost_capacity_factor >= 0 && d.defrost_capacity_factor <= 1;
}
double heat_capacity_kw(const Parameters& p, double outdoor_c) {
    return std::max(p.minimum_heat_kw,
        p.heat_kw_at_zero_c + p.heat_kw_change_per_outdoor_c * outdoor_c);
}
double heat_cop(const Parameters& p, double outdoor_c) {
    return std::clamp(p.cop_at_zero_c +
        p.cop_change_per_outdoor_c * outdoor_c, p.minimum_cop, p.maximum_cop);
}
Step advance(const Parameters& p, const State& s, const Period& d, bool heat_16_c) {
    Step out{};
    out.next = s;
    if (!valid(p,s,d)) {
        out.next.indoor_c = std::numeric_limits<double>::quiet_NaN();
        out.electric_kwh = std::numeric_limits<double>::quiet_NaN();
        out.cost_eur = std::numeric_limits<double>::quiet_NaN();
        return out;
    }
    const bool starting = heat_16_c && !s.heat_requested;
    double run_fraction = starting ?
        1.0 - p.startup_delay_minutes / slot_minutes : 1.0;
    // At 16 C, the pump's own thermostat may remove compressor demand.
    // This simplified limit must be re-identified with actual CN105 readings.
    if (s.indoor_c >= 16.0) run_fraction = 0;
    else if (s.indoor_c > 15.5)
        run_fraction *= (16.0 - s.indoor_c) / 0.5;
    out.delivered_thermal_kw = heat_16_c ?
        heat_capacity_kw(p,d.outdoor_c) * d.defrost_capacity_factor * run_fraction : 0;
    const double alpha=std::exp(-p.loss_kw_per_c * slot_hours / p.thermal_mass_kwh_per_c);
    // After-heat of a HEAT->OFF transition is spread over that OFF slot.
    const bool stopping = !heat_16_c && s.heat_requested;
    const double afterheat_kw = stopping ? p.stop_afterheat_kwh / slot_hours : 0;
    const double gain=out.delivered_thermal_kw + d.internal_gain_kw + afterheat_kw;
    out.next.indoor_c=d.outdoor_c + (s.indoor_c-d.outdoor_c)*alpha +
        gain / p.loss_kw_per_c * (1.0-alpha);
    out.next.heat_requested=heat_16_c;
    out.next.minutes_in_mode=heat_16_c==s.heat_requested ?
        std::min(600u,s.minutes_in_mode+slot_minutes) : slot_minutes;
    out.electric_kwh=slot_hours * (heat_16_c ? p.heat_standby_kw : p.off_standby_kw);
    if (heat_16_c) out.electric_kwh +=
        slot_hours*out.delivered_thermal_kw / heat_cop(p,d.outdoor_c);
    if (starting) out.electric_kwh += p.startup_electric_kwh;
    out.cost_eur=out.electric_kwh*d.total_price_eur_per_kwh;
    return out;
}
} // namespace enevina::cottage
