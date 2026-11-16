#pragma once
#include <cstddef>
#include <cstdint>

namespace enevina::cottage {
// Calibrated/site-provided parameters, NOT Mitsubishi manufacturer's performance curves.
// kW, kWh, Celsius, EUR/kWh; all slots are 15 minutes.
struct Parameters {
    double thermal_mass_kwh_per_c = 0;
    double loss_kw_per_c = 0;
    double heat_kw_at_zero_c = 0;
    double heat_kw_change_per_outdoor_c = 0;
    double minimum_heat_kw = 0;
    double cop_at_zero_c = 0;
    double cop_change_per_outdoor_c = 0;
    double minimum_cop = 0;
    double maximum_cop = 0;
    double heat_standby_kw = 0;
    double off_standby_kw = 0;
    double startup_electric_kwh = 0;
    double startup_delay_minutes = 0;
    double planning_min_c = 8.0;
    double critical_c = 5.0;
    double planning_uncertainty_c = 0;
    double maximum_indoor_c = 15.8;
    double switching_penalty_eur = 0.02;
    unsigned min_heat_minutes = 60;
    unsigned min_off_minutes = 60;
};
struct State {
    double indoor_c = 0;
    bool heat_requested = false;
    unsigned minutes_in_mode = 0;
};
struct Period {
    double outdoor_c = 0;
    double total_price_eur_per_kwh = 0;
    double internal_gain_kw = 0;
    double defrost_capacity_factor = 1; // Forecast uncertainty must be handled separately.
};
struct Step {
    State next{};
    double delivered_thermal_kw = 0;
    double electric_kwh = 0;
    double cost_eur = 0;
};
constexpr double slot_hours = 0.25;
constexpr unsigned slot_minutes = 15;
bool valid(const Parameters& p);
bool valid(const Parameters& p, const State& s, const Period& d);
double heat_capacity_kw(const Parameters& p, double outdoor_c);
double heat_cop(const Parameters& p, double outdoor_c);
Step advance(const Parameters& p, const State& s, const Period& d, bool heat_16_c);
} // namespace enevina::cottage
