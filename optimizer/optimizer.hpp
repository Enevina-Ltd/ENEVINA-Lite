#pragma once

#include "physics/physics.hpp"
#include <array>
#include <cstddef>

namespace enevina {
constexpr size_t max_periods = 192;
enum class OptimizeStatus { Ok, InvalidInput, Infeasible, ResourceLimit };
struct OptimizeResult {
    OptimizeStatus status = OptimizeStatus::InvalidInput;
    size_t count = 0;
    double cost_eur = 0;
    std::array<Action, max_periods> actions{};
};

// Bounded backward dynamic program. No network, device, or wall-clock calls.
OptimizeResult optimize(const Parameters& p, State initial,
                        const Period* periods, size_t count,
                        size_t max_grid_states = 8192);
}
