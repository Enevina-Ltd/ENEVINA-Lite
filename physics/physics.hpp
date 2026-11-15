#pragma once

#include <cstdint>

namespace enevina {
constexpr double step_hours = 0.25;

// All powers are kW, energies kWh, prices EUR/kWh, temperatures Celsius.
// Coefficients must be measured or explicitly configured for the installation.
struct Parameters {
    double building_capacity_kwh_per_c = 0;
    double building_loss_kw_per_c = 0;
    double tank_capacity_kwh = 0;
    double tank_loss_kw_per_kwh = 0;
    double pump_max_thermal_kw = 0;
    double pump_derate_kw_per_c_below_zero = 0;
    double cop_at_reference = 0; // reference: outdoor 0 C, supply 35 C
    double cop_outdoor_per_c = 0;
    double cop_supply_per_c = 0;
    double minimum_cop = 0;
    double maximum_cop = 0;
    double emitter_kw_per_c = 0;
    double space_supply_low_c = 35;
    double space_supply_high_c = 45;
    double dhw_supply_c = 50;
    double indoor_min_c = 0;
    double indoor_max_c = 0;
    double tank_min_kwh = 0;
};

struct State { double indoor_c; double tank_kwh; };
struct Period {
    double outdoor_c;
    double total_price_eur_per_kwh;
    double internal_gain_kw;
    double dhw_draw_kw;
    double tank_ambient_c; // reserved for calibrated ambient-dependent loss
};
enum class Mode : uint8_t { Idle, Space, Dhw };
struct Action { Mode mode; double requested_thermal_kw; double supply_c; };
struct Step {
    State next;
    double delivered_thermal_kwh;
    double electric_kwh;
    double cost_eur;
};

bool valid(const Parameters& p);
double cop(const Parameters& p, double outdoor_c, double supply_c);
double available_thermal_kw(const Parameters& p, double outdoor_c);
Step advance(const Parameters& p, State state, Period period, Action action);
bool feasible(const Parameters& p, State state);
}
