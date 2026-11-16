#include "cottage/mpc.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace enevina::cottage {
namespace {
constexpr double quantum_c=0.10;
constexpr uint16_t no_parent=65535;
struct Node {
    double objective=std::numeric_limits<double>::infinity();
    double temperature=0;
};
size_t key(size_t bin, bool on, size_t age, size_t age_dimension) {
    return (bin*2+(on?1u:0u))*age_dimension+age;
}
uint64_t steady_now_ms(void*) {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count());
}
MpcResult interrupted(MpcStatus status) {
    MpcResult result{};
    result.status=status;
    // No partial plan, even if interruption occurs during backtracking/replay.
    return result;
}
}

MpcResult optimize(const Parameters& p, const State& initial,
    const Period* periods, size_t count, size_t memory_budget_bytes,
    const MpcRunControls& controls) {
    MpcResult result;
    if (!valid(p) || !periods || count==0 || count>max_horizon_slots ||
        !std::isfinite(initial.indoor_c) ||
        initial.indoor_c > p.maximum_indoor_c ||
        initial.indoor_c < p.critical_c ||
        memory_budget_bytes==0)
        return result;
    const auto clock=controls.monotonic_now_ms ?
        controls.monotonic_now_ms : steady_now_ms;
    const uint64_t started=clock(controls.clock_context);
    uint64_t last_tick=started;
    size_t checks=0,unchanged_ticks=0;
    auto check_interrupt=[&](bool force_clock=false)->MpcStatus {
        if(controls.cancel_requested &&
           controls.cancel_requested(controls.cancel_context))
            return MpcStatus::Cancelled;
        // Check clock during expansion, not only between horizon slots.
        if(!force_clock && (++checks%32)!=0) return MpcStatus::Ok;
        const uint64_t now=clock(controls.clock_context);
        // A backwards jump (including a wrapped 32-bit provider) fails closed.
        if(now<last_tick || now<started) return MpcStatus::Timeout;
        if(now==last_tick) {
            // A stalled clock must not allow an unbounded search.
            if(++unchanged_ticks>8192) return MpcStatus::Timeout;
        } else unchanged_ticks=0;
        last_tick=now;
        if(controls.runtime_budget_ms==0 ||
           now-started>=controls.runtime_budget_ms)
            return MpcStatus::Timeout;
        return MpcStatus::Ok;
    };
    if(const auto stop=check_interrupt(true);stop!=MpcStatus::Ok)
        return interrupted(stop);
    const double min_c=p.planning_min_c+p.planning_uncertainty_c;
    if (initial.indoor_c < min_c-1e-9) {
        result.status=MpcStatus::UnsafeStart;
        return result;
    }
    for(size_t i=0;i<count;i++) {
        if (!valid(p, initial, periods[i])) return result;
    }
    const size_t min_on=p.min_heat_minutes/slot_minutes;
    const size_t min_off=p.min_off_minutes/slot_minutes;
    const size_t age_max=std::max(min_on,min_off);
    const size_t age_dim=age_max+1;
    const double grid_min=p.critical_c-1.0;
    const size_t bins=static_cast<size_t>(
        std::ceil((p.maximum_indoor_c-grid_min)/quantum_c))+2;
    // Guard integer multiplication before allocation, even for a huge budget.
    if(age_dim==0 || age_dim>no_parent/2 ||
       bins>=no_parent/(2*age_dim)) {
        result.status=MpcStatus::ResourceLimit;
        return result;
    }
    const size_t states=bins*2*age_dim;
    const size_t bytes_per_state=count*sizeof(uint16_t)+2*sizeof(Node);
    if(states>=no_parent || states>memory_budget_bytes/bytes_per_state) {
        result.status=MpcStatus::ResourceLimit;
        return result;
    }
    const auto bin_of=[&](double t)->size_t {
        const auto b=static_cast<long>(std::lround((t-grid_min)/quantum_c));
        return static_cast<size_t>(std::clamp(b,0L,static_cast<long>(bins-1)));
    };
    std::vector<Node> current(states), next(states);
    std::vector<uint16_t> parent(count*states,no_parent);
    const size_t initial_age=std::min<size_t>(
        initial.minutes_in_mode/slot_minutes,age_max);
    const size_t first=key(bin_of(initial.indoor_c),
        initial.heat_requested,initial_age,age_dim);
    current[first]={0.0,initial.indoor_c};
    for(size_t t=0;t<count;t++) {
        if(const auto stop=check_interrupt(true);stop!=MpcStatus::Ok)
            return interrupted(stop);
        std::fill(next.begin(),next.end(),Node{});
        bool any=false;
        for(size_t from=0;from<states;from++) {
            if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
                return interrupted(stop);
            const Node node=current[from];
            if (!std::isfinite(node.objective)) continue;
            const size_t age=from%age_dim;
            const bool prior_on=((from/age_dim)%2)!=0;
            for(unsigned choice=0;choice<2;choice++) {
                if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
                    return interrupted(stop);
                const bool on=(choice!=0);
                const bool changed=(on!=prior_on);
                if (changed && age < (prior_on?min_on:min_off)) continue;
                const State source{node.temperature,prior_on,
                    static_cast<unsigned>(age*slot_minutes)};
                const Step step=advance(p,source,periods[t],on);
                const double temp=step.next.indoor_c;
                if (!std::isfinite(temp) ||
                    temp < min_c-1e-9 || temp > p.maximum_indoor_c+1e-9)
                    continue;
                const size_t new_age=changed?1:std::min(age+1,age_max);
                const size_t to=key(bin_of(temp),on,new_age,age_dim);
                const double objective=node.objective+step.cost_eur+
                    (changed?p.switching_penalty_eur:0.0);
                // Deterministic tie-break; preserve temperature for terminal margin.
                if (objective < next[to].objective-1e-10 ||
                    (std::abs(objective-next[to].objective)<=1e-10 &&
                     temp>next[to].temperature)) {
                    next[to]={objective,temp};
                    parent[t*states+to]=static_cast<uint16_t>(from);
                    any=true;
                }
            }
        }
        if(const auto stop=check_interrupt(true);stop!=MpcStatus::Ok)
            return interrupted(stop);
        if(!any) { result.status=MpcStatus::Infeasible; return result; }
        current.swap(next);
    }
    const double end_min=std::max(min_c,initial.indoor_c)-1e-8;
    size_t best=states;
    for(size_t i=0;i<states;i++) {
        if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
            return interrupted(stop);
        if(current[i].temperature < end_min ||
           !std::isfinite(current[i].objective)) continue;
        if(best==states || current[i].objective <
            current[best].objective-1e-10 ||
           (std::abs(current[i].objective-current[best].objective)<=1e-10 &&
            current[i].temperature>current[best].temperature)) best=i;
    }
    if(best==states) {result.status=MpcStatus::Infeasible;return result;}
    result.count=count;
    for(size_t t=count;t>0;t--) {
        if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
            return interrupted(stop);
        result.heat_16_c[t-1]=static_cast<uint8_t>((best/age_dim)%2);
        const auto prev=parent[(t-1)*states+best];
        if(prev==no_parent) {result.status=MpcStatus::ReplayFailed;return result;}
        best=prev;
    }
    // Exact, unquantized replay: DP bins must never manufacture thermal energy.
    State replay=initial;
    double lowest=initial.indoor_c, cost=0, electric=0;
    unsigned starts=0;
    for(size_t t=0;t<count;t++) {
        if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
            return interrupted(stop);
        const bool on=result.heat_16_c[t]!=0;
        if(on!=replay.heat_requested) {
            const unsigned required=replay.heat_requested ?
                p.min_heat_minutes:p.min_off_minutes;
            if(replay.minutes_in_mode<required) {
                result.status=MpcStatus::ReplayFailed;return result;
            }
            if(on) starts++;
        }
        const auto step=advance(p,replay,periods[t],on);
        replay=step.next;
        lowest=std::min(lowest,replay.indoor_c);
        electric+=step.electric_kwh;
        cost+=step.cost_eur;
        if(!std::isfinite(replay.indoor_c) ||
            replay.indoor_c<min_c-1e-8 ||
            replay.indoor_c>p.maximum_indoor_c+1e-8) {
            result.status=MpcStatus::ReplayFailed;return result;
        }
    }
    if(replay.indoor_c<end_min-1e-8) {
        result.status=MpcStatus::ReplayFailed;return result;
    }
    if(const auto stop=check_interrupt(true);stop!=MpcStatus::Ok)
        return interrupted(stop);
    result.status=MpcStatus::Ok;
    result.cost_eur=cost;
    result.electric_kwh=electric;
    result.minimum_c=lowest;
    result.final_c=replay.indoor_c;
    result.starts=starts;
    return result;
}
} // namespace enevina::cottage
