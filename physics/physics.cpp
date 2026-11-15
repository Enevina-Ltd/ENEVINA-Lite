#include "physics/physics.hpp"
#include <algorithm>
#include <cmath>

namespace enevina {
bool valid(const Parameters& p) {
    return std::isfinite(p.building_capacity_kwh_per_c) && p.building_capacity_kwh_per_c > 0 &&
           std::isfinite(p.building_loss_kw_per_c) && p.building_loss_kw_per_c >= 0 &&
           std::isfinite(p.tank_capacity_kwh) && p.tank_capacity_kwh > 0 &&
           std::isfinite(p.tank_loss_kw_per_kwh) && p.tank_loss_kw_per_kwh >= 0 &&
           std::isfinite(p.pump_max_thermal_kw) && p.pump_max_thermal_kw > 0 &&
           std::isfinite(p.pump_derate_kw_per_c_below_zero) &&
           p.pump_derate_kw_per_c_below_zero >= 0 &&
           std::isfinite(p.cop_at_reference) && p.cop_at_reference >= 1 &&
           std::isfinite(p.cop_outdoor_per_c) && p.cop_outdoor_per_c >= 0 &&
           std::isfinite(p.cop_supply_per_c) && p.cop_supply_per_c >= 0 &&
           std::isfinite(p.minimum_cop) && p.minimum_cop >= 1 &&
           std::isfinite(p.maximum_cop) && p.maximum_cop >= p.minimum_cop &&
           std::isfinite(p.emitter_kw_per_c) && p.emitter_kw_per_c > 0 &&
           std::isfinite(p.space_supply_low_c) &&
           std::isfinite(p.space_supply_high_c) &&
           std::isfinite(p.dhw_supply_c) &&
           p.space_supply_low_c >= 20 && p.space_supply_high_c >= p.space_supply_low_c &&
           p.dhw_supply_c >= p.space_supply_low_c &&
           std::isfinite(p.indoor_min_c) && std::isfinite(p.indoor_max_c) &&
           p.indoor_max_c > p.indoor_min_c && p.tank_min_kwh >= 0 &&
           p.tank_capacity_kwh > p.tank_min_kwh;
}

double cop(const Parameters& p, double outdoor_c, double supply_c) {
    const double estimate = p.cop_at_reference + p.cop_outdoor_per_c * outdoor_c
                           - p.cop_supply_per_c * (supply_c - 35.0);
    return std::clamp(estimate, p.minimum_cop, p.maximum_cop);
}

double available_thermal_kw(const Parameters& p, double outdoor_c) {
    return std::max(0.0, p.pump_max_thermal_kw -
        p.pump_derate_kw_per_c_below_zero * std::max(0.0, -outdoor_c));
}

Step advance(const Parameters& p, State state, Period period, Action action) {
    double delivered_kw = 0;
    if (action.mode != Mode::Idle) {
        delivered_kw = std::min(std::max(0.0, action.requested_thermal_kw),
                                available_thermal_kw(p, period.outdoor_c));
        if (action.mode == Mode::Space) {
            // Emitter rating is referenced to the configured indoor set point.
            const double reference_indoor_c = (p.indoor_min_c + p.indoor_max_c) / 2;
            delivered_kw = std::min(delivered_kw,
                p.emitter_kw_per_c * std::max(0.0, action.supply_c - reference_indoor_c));
        }
    }
    const double space_kw = action.mode == Mode::Space ? delivered_kw : 0;
    const double dhw_kw = action.mode == Mode::Dhw ? delivered_kw : 0;
    const double building_loss_kw = p.building_loss_kw_per_c *
                                     (state.indoor_c - period.outdoor_c);
    const double tank_loss_kw = p.tank_loss_kw_per_kwh * state.tank_kwh;
    Step result{};
    result.next.indoor_c = state.indoor_c + step_hours *
        (space_kw + period.internal_gain_kw - building_loss_kw) /
        p.building_capacity_kwh_per_c;
    result.next.tank_kwh = state.tank_kwh + step_hours *
        (dhw_kw - period.dhw_draw_kw - tank_loss_kw);
    result.delivered_thermal_kwh = delivered_kw * step_hours;
    result.electric_kwh = delivered_kw > 0 ? result.delivered_thermal_kwh /
        cop(p, period.outdoor_c, action.supply_c) : 0;
    result.cost_eur = result.electric_kwh * period.total_price_eur_per_kwh;
    return result;
}

bool feasible(const Parameters& p, State state) {
    constexpr double epsilon = 1e-7;
    return std::isfinite(state.indoor_c) && std::isfinite(state.tank_kwh) &&
           state.indoor_c >= p.indoor_min_c - epsilon &&
           state.indoor_c <= p.indoor_max_c + epsilon &&
           state.tank_kwh >= p.tank_min_kwh - epsilon &&
           state.tank_kwh <= p.tank_capacity_kwh + epsilon;
}
}
