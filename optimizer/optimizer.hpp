#pragma once

#include "physics/physics.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace enevina {
constexpr size_t max_periods = 192;
enum class OptimizeStatus { Ok, InvalidInput, Infeasible, ResourceLimit, WorkLimit, Cancelled };
struct OptimizeResult {
    OptimizeStatus status = OptimizeStatus::InvalidInput;
    size_t count = 0;
    double cost_eur = 0;
    std::array<Action, max_periods> actions{};
};

// Independent of ESP-IDF. Callbacks must be nonblocking and side-effect free.
struct OptimizeRunControls {
    size_t max_work_units=20000000; // counts expansions, candidate replay steps
    bool (*cancel_requested)(void*)=nullptr;
    void* cancel_context=nullptr;
};

// Bounded deterministic forward search. No network, device, or wall-clock calls.
OptimizeResult optimize(const Parameters& p, State initial,
                        const Period* periods, size_t count,
                        size_t max_grid_states = 8192,
                        const OptimizeRunControls& controls={});
}
