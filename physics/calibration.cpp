#include "physics/calibration.hpp"
#include "physics/physics.hpp"
#include <cmath>

namespace enevina {
bool fit_building(const BuildingSample* samples, size_t count,
                  BuildingCalibration& output) {
    if (!samples || count < 4) return false;
    double qq=0, ql=0, ll=0, qy=0, ly=0;
    for (size_t i=0; i<count; ++i) {
        const auto& s=samples[i];
        const double q=s.delivered_heat_kw+s.internal_gain_kw;
        const double l=s.indoor_c-s.outdoor_c;
        const double y=(s.next_indoor_c-s.indoor_c)/step_hours;
        if (!std::isfinite(q) || !std::isfinite(l) || !std::isfinite(y)) return false;
        qq+=q*q; ql+=q*l; ll+=l*l; qy+=q*y; ly+=l*y;
    }
    const double determinant=qq*ll-ql*ql;
    if (determinant <= 1e-10*qq*ll) return false;
    const double inverse_capacity=(qy*ll-ly*ql)/determinant;
    const double negative_loss_over_capacity=(ly*qq-qy*ql)/determinant;
    if (inverse_capacity <= 0 || negative_loss_over_capacity > 0) return false;
    const double capacity=1/inverse_capacity;
    const double loss=-negative_loss_over_capacity/inverse_capacity;
    if (!std::isfinite(capacity) || !std::isfinite(loss)) return false;
    output={capacity,loss};
    return true;
}
}
