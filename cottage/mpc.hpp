#pragma once
#include "cottage/physics.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace enevina::cottage {
constexpr size_t max_horizon_slots = 192;
enum class MpcStatus : uint8_t {
    Ok, InvalidInput, UnsafeStart, Infeasible, ResourceLimit, ReplayFailed,
    Timeout, Cancelled
};
struct MpcResult {
    MpcStatus status=MpcStatus::InvalidInput;
    size_t count=0;
    double cost_eur=0;
    double electric_kwh=0;
    double minimum_c=0;
    double final_c=0;
    unsigned starts=0;
    // Legacy heat flags for callers of the original fixed-setpoint mode.
    std::array<uint8_t,max_horizon_slots> heat_16_c{};
    std::array<uint16_t,max_horizon_slots> setpoint_c100{}; // zero: OFF
};
// Each platform may provide its own 64-bit monotonic millisecond clock.
 // nullptr clock uses C++ steady_clock. Callbacks must be nonblocking.
struct MpcRunControls {
    uint32_t runtime_budget_ms=2000;
    uint64_t (*monotonic_now_ms)(void*)=nullptr;
    void* clock_context=nullptr;
    bool (*cancel_requested)(void*)=nullptr;
    void* cancel_context=nullptr;
    // Terminal condition. NaN (default): the plan must end >= the initial
    // temperature (equal-terminal open-loop comparisons, unchanged). A
    // finite value instead requires end >= max(floor, terminal_min_c): used
    // by receding-horizon control, where "end >= now" ratchets the room up
    // after every pre-heat and can make the next plan infeasible.
    double terminal_min_c=__builtin_nan("");
    // The planner supplies a sorted set of adapter-approved heating setpoints.
    // The legacy fixed-setpoint mode still considers only 16 C and OFF.
    std::array<double,4> heat_setpoints_c{};
    size_t heat_setpoint_count=0;
    bool permit_off=true;
    double initial_setpoint_c=0;
    double setpoint_change_penalty=0;
    // Optional paired, suitably aligned workspace allocator for host fault
    // injection. Default firmware allocations are PSRAM-only, fallible.
    void* (*workspace_alloc)(size_t,void*)=nullptr;
    void (*workspace_free)(void*,void*)=nullptr;
    void* workspace_context=nullptr;
};
MpcResult optimize(const Parameters& p, const State& initial,
    const Period* periods, size_t count, size_t memory_budget_bytes=3*1024*1024,
    const MpcRunControls& controls={});
}
