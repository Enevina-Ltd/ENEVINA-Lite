#pragma once
#include <cstddef>

namespace enevina {
struct BuildingSample {
    double indoor_c;
    double outdoor_c;
    double delivered_heat_kw;
    double internal_gain_kw;
    double next_indoor_c;
};
struct BuildingCalibration {
    double capacity_kwh_per_c = 0;
    double loss_kw_per_c = 0;
};
// Least-squares identification of C and UA in the same heat balance as advance().
// Returns false for unexcited, invalid, or physically impossible observations.
bool fit_building(const BuildingSample* samples, size_t count,
                  BuildingCalibration& output);
}
