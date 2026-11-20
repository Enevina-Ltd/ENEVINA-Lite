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
    // Search only: ESP32-S3 has hardware FP32 but software-emulated FP64.
    // Final trajectory still uses double-precision physics::advance().
    float objective=std::numeric_limits<float>::infinity();
    float temperature=0;
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
        memory_budget_bytes==0 ||
        (std::isfinite(controls.terminal_min_c) &&
         controls.terminal_min_c > p.maximum_indoor_c))
        return result;
    const auto clock=controls.monotonic_now_ms ?
        controls.monotonic_now_ms : steady_now_ms;
    const uint64_t started=clock(controls.clock_context);
    uint64_t last_tick=started;
    size_t checks=0,unchanged_ticks=0;
    auto check_interrupt=[&](bool force_clock=false)->MpcStatus {
        // Poll both cancellation and monotonic deadline every 32
        // expansions, and at every forced boundary. This preserves
        // sub-millisecond cancellation latency at our bounded state
        // count without performing a FreeRTOS/millis call per candidate.
        if(!force_clock && (++checks%32)!=0) return MpcStatus::Ok;
        if(controls.cancel_requested &&
           controls.cancel_requested(controls.cancel_context))
            return MpcStatus::Cancelled;
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
    // Keep two representatives per quantized bin/mode/dwell state:
    // minimum cost and maximum actual temperature. A cheaper colder state
    // alone can destroy the only exact terminal-feasible trajectory.
    // All allocations, including terminal indexes, obey the byte budget.
    constexpr size_t representatives=2;
    const size_t age_multiplier=2*representatives;
    if(age_dim==0 || age_dim>=no_parent/age_multiplier ||
       bins>=no_parent/(age_multiplier*age_dim)) {
        result.status=MpcStatus::ResourceLimit;
        return result;
    }
    const size_t states=bins*age_multiplier*age_dim;
    const size_t bytes_per_state=count*sizeof(uint16_t)+
        2*sizeof(Node)+3*sizeof(uint16_t); // terminal + 2 active frontiers
    if(states>=no_parent || states>memory_budget_bytes/bytes_per_state) {
        result.status=MpcStatus::ResourceLimit;
        return result;
    }
    const float grid_min_f=static_cast<float>(grid_min);
    const float inverse_bin=1.0f/static_cast<float>(quantum_c);
    const auto bin_of=[&](float t)->size_t {
        // All admitted temperatures are >= critical_c > grid_min.
        // Half-up rounding matches lround() for these nonnegative bins.
        const float scaled=(t-grid_min_f)*inverse_bin;
        const auto b=static_cast<long>(scaled+0.5f);
        return static_cast<size_t>(std::clamp(b,0L,static_cast<long>(bins-1)));
    };
    std::vector<Node> current(states), next(states);
    std::vector<uint16_t> parent(count*states,no_parent);
    const size_t initial_age=std::min<size_t>(
        initial.minutes_in_mode/slot_minutes,age_max);
    const size_t first=representatives*key(bin_of(initial.indoor_c),
        initial.heat_requested,initial_age,age_dim);
    current[first]={0.0f,static_cast<float>(initial.indoor_c)};
    // Traverse only reachable quantized states; a full-grid sweep
    // was the dominant runtime cost for cold/price-spike 48h cases.
    std::vector<uint16_t> active,next_active;
    active.reserve(states);
    next_active.reserve(states);
    active.push_back(static_cast<uint16_t>(first));
    // State-independent physics precomputed once per forecast period.
    // Exact replay below still runs the unmodified advance() model.
    const double exact_alpha=std::exp(-p.loss_kw_per_c*slot_hours/
                                      p.thermal_mass_kwh_per_c);
    const float alpha=static_cast<float>(exact_alpha);
    const float beta=static_cast<float>((1.0-exact_alpha)/p.loss_kw_per_c);
    const float startup_fraction=static_cast<float>(
        1.0-p.startup_delay_minutes/slot_minutes);
    const float standby_heat=static_cast<float>(p.heat_standby_kw);
    const float standby_off=static_cast<float>(p.off_standby_kw);
    const float startup_electric=static_cast<float>(p.startup_electric_kwh);
    const float switching_penalty=static_cast<float>(p.switching_penalty_eur);
    // Same HEAT->OFF after-heat term as advance(); 0 leaves the search unchanged.
    const float afterheat=beta*static_cast<float>(p.stop_afterheat_kwh/slot_hours);
    const float search_min=static_cast<float>(min_c);
    const float search_max=static_cast<float>(p.maximum_indoor_c);
    for(size_t t=0;t<count;t++) {
        if(const auto stop=check_interrupt(true);stop!=MpcStatus::Ok)
            return interrupted(stop);
        const auto& period=periods[t];
        const float input=static_cast<float>(period.outdoor_c)*(1.0f-alpha) +
            beta*static_cast<float>(period.internal_gain_kw);
        const float thermal_max=static_cast<float>(
            heat_capacity_kw(p,period.outdoor_c)*period.defrost_capacity_factor);
        const float thermal_to_electric=static_cast<float>(
            slot_hours/heat_cop(p,period.outdoor_c));
        const float price=static_cast<float>(period.total_price_eur_per_kwh);
        std::fill(next.begin(),next.end(),Node{});
        next_active.clear();
        bool any=false;
        for(const uint16_t from_index:active) {
            const size_t from=from_index;
            if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
                return interrupted(stop);
            const Node node=current[from];
            if(!std::isfinite(node.objective)) continue;
            const size_t physical_state=from/representatives;
            const size_t age=physical_state%age_dim;
            const bool prior_on=((physical_state/age_dim)%2)!=0;
            for(unsigned choice=0;choice<2;choice++) {
                if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
                    return interrupted(stop);
                const bool on=choice!=0;
                const bool changed=on!=prior_on;
                if(changed && age<(prior_on?min_on:min_off)) continue;
                const bool starting=on && !prior_on;
                float thermal=on ? thermal_max*(starting?startup_fraction:1.0f) : 0.0f;
                if(on && node.temperature>=16.0f) thermal=0.0f;
                else if(on && node.temperature>15.5f)
                    thermal*=(16.0f-node.temperature)/0.5f;
                const float temp=alpha*node.temperature+input+beta*thermal+
                    (prior_on && !on ? afterheat : 0.0f);
                // Small FP32 search tolerance only. Exact FP64 replay below
                // rejects out-of-bounds candidates before returning Ok.
                if(!std::isfinite(temp) ||
                   temp<search_min-1e-4f || temp>search_max+1e-4f)
                    continue;
                const size_t new_age=changed?1:std::min(age+1,age_max);
                const size_t to=representatives*key(bin_of(temp),on,new_age,age_dim);
                const float electricity=static_cast<float>(slot_hours) *
                    (on?standby_heat:standby_off) +
                    (on?thermal*thermal_to_electric:0.0f) +
                    (starting?startup_electric:0.0f);
                const float objective=node.objective+electricity*price+
                    (changed?switching_penalty:0.0f);
                if(!std::isfinite(objective)) continue;
                auto retain=[&](size_t slot) {
                    if(!std::isfinite(next[slot].objective))
                        next_active.push_back(static_cast<uint16_t>(slot));
                    next[slot]={objective,temp};
                    parent[t*states+slot]=static_cast<uint16_t>(from);
                    any=true;
                };
                // The first slot prioritizes cost; temperature breaks ties.
                if(!std::isfinite(next[to].objective) ||
                   objective<next[to].objective-1e-6f ||
                   (std::abs(objective-next[to].objective)<=1e-6f &&
                    temp>next[to].temperature))
                    retain(to);
                // The second slot prioritizes a feasible thermal margin,
                // breaking equal temperatures by the lower objective.
                if(!std::isfinite(next[to+1].objective) ||
                   temp>next[to+1].temperature+1e-5f ||
                   (std::abs(temp-next[to+1].temperature)<=1e-5f &&
                    objective<next[to+1].objective-1e-6f))
                    retain(to+1);
            }
        }
        if(const auto stop=check_interrupt(true);stop!=MpcStatus::Ok)
            return interrupted(stop);
        if(!any) {result.status=MpcStatus::Infeasible;return result;}
        // Preserve the original ascending-index visit order so all DP
        // tie breaks (and therefore benchmark costs) remain identical.
        std::sort(next_active.begin(),next_active.end());
        current.swap(next);
        active.swap(next_active);
    }
    const double end_min=(std::isfinite(controls.terminal_min_c)?
        std::max(min_c,controls.terminal_min_c):std::max(min_c,initial.indoor_c))-1e-8;
    const float search_end_min=static_cast<float>(end_min)-1e-4f;
    std::vector<uint16_t> terminals;
    terminals.reserve(states);
    for(const uint16_t i:active) {
        if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
            return interrupted(stop);
        if(std::isfinite(current[i].objective) &&
           current[i].temperature>=search_end_min)
            terminals.push_back(i);
    }
    if(terminals.empty()) {result.status=MpcStatus::Infeasible;return result;}
    std::sort(terminals.begin(),terminals.end(),[&](uint16_t a,uint16_t b) {
        if(current[a].objective!=current[b].objective)
            return current[a].objective<current[b].objective;
        if(current[a].temperature!=current[b].temperature)
            return current[a].temperature>current[b].temperature;
        return a<b;
    });
    // An approximate DP survivor is not accepted without exact replay.
    // If the cheapest survivor fails, try the next one in cost order.
    for(const uint16_t finalist:terminals) {
        if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
            return interrupted(stop);
        MpcResult candidate{};
        candidate.count=count;
        size_t at=finalist;
        bool replayable=true;
        for(size_t t=count;t>0;t--) {
            if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
                return interrupted(stop);
            const size_t physical_state=at/representatives;
            candidate.heat_16_c[t-1]=
                static_cast<uint8_t>((physical_state/age_dim)%2);
            const auto prev=parent[(t-1)*states+at];
            if(prev==no_parent) {replayable=false;break;}
            at=prev;
        }
        if(!replayable) continue;
        State replay=initial;
        double lowest=initial.indoor_c,cost=0,electric=0;
        unsigned starts=0;
        for(size_t t=0;t<count;t++) {
            if(const auto stop=check_interrupt();stop!=MpcStatus::Ok)
                return interrupted(stop);
            const bool on=candidate.heat_16_c[t]!=0;
            if(on!=replay.heat_requested) {
                const unsigned required=replay.heat_requested ?
                    p.min_heat_minutes:p.min_off_minutes;
                if(replay.minutes_in_mode<required) {
                    replayable=false;
                    break;
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
                replayable=false;
                break;
            }
        }
        if(!replayable || replay.indoor_c<end_min-1e-8) continue;
        if(const auto stop=check_interrupt(true);stop!=MpcStatus::Ok)
            return interrupted(stop);
        candidate.status=MpcStatus::Ok;
        candidate.cost_eur=cost;
        candidate.electric_kwh=electric;
        candidate.minimum_c=lowest;
        candidate.final_c=replay.indoor_c;
        candidate.starts=starts;
        return candidate;
    }
    result.status=MpcStatus::ReplayFailed;
    return result;
}
} // namespace enevina::cottage
