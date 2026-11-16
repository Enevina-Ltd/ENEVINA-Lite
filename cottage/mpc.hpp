#pragma once
#include "cottage/physics.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace enevina::cottage {
constexpr size_t max_horizon_slots = 192;
enum class MpcStatus : uint8_t {
    Ok, InvalidInput, UnsafeStart, Infeasible, ResourceLimit, ReplayFailed
};
struct MpcResult {
    MpcStatus status=MpcStatus::InvalidInput;
    size_t count=0;
    double cost_eur=0;
    double electric_kwh=0;
    double minimum_c=0;
    double final_c=0;
    unsigned starts=0;
    // false: OFF; true: HEAT 16 C. NO physical command emission.
    std::array<uint8_t,max_horizon_slots> heat_16_c{};
};
MpcResult optimize(const Parameters& p, const State& initial,
    const Period* periods, size_t count, size_t memory_budget_bytes=3*1024*1024);
}
