#include "optimizer/optimizer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace enevina {
namespace {
constexpr double room_bin_c = 0.1;
constexpr double tank_bin_kwh = 0.25;
constexpr double infinity = std::numeric_limits<double>::infinity();

std::array<Action, 7> actions(const Parameters& p, double outdoor_c) {
    const double half = available_thermal_kw(p, outdoor_c) / 2;
    const double full = available_thermal_kw(p, outdoor_c);
    return {{{Mode::Idle, 0, p.space_supply_low_c},
             {Mode::Space, half, p.space_supply_low_c},
             {Mode::Space, full, p.space_supply_low_c},
             {Mode::Space, half, p.space_supply_high_c},
             {Mode::Space, full, p.space_supply_high_c},
             {Mode::Dhw, half, p.dhw_supply_c},
             {Mode::Dhw, full, p.dhw_supply_c}}};
}

// Exact physical states are propagated forward; only near-identical states
// are merged. This recovers trajectories that a rounded backward policy loses.
OptimizeResult forward_exact(const Parameters& p, State initial,
                             const Period* periods, size_t count,
                             size_t max_grid_states) {
    OptimizeResult result{};
    constexpr double forward_room_bin_c=room_bin_c/2;
    constexpr double forward_tank_bin_kwh=tank_bin_kwh/2;
    const size_t room_count = static_cast<size_t>(std::ceil(
        (p.indoor_max_c-p.indoor_min_c)/forward_room_bin_c))+1;
    const size_t tank_count = static_cast<size_t>(std::ceil(
        (p.tank_capacity_kwh-p.tank_min_kwh)/forward_tank_bin_kwh))+1;
    if (!tank_count || room_count > max_grid_states/tank_count ||
        room_count*tank_count > 500000/count ||
        room_count*tank_count > UINT16_MAX) {
        result.status=OptimizeStatus::ResourceLimit; return result;
    }
    const size_t grid_size=room_count*tank_count;
    auto bin = [&](State s) {
        const size_t r=static_cast<size_t>(std::clamp(std::round(
            (s.indoor_c-p.indoor_min_c)/forward_room_bin_c),0.0,double(room_count-1)));
        const size_t w=static_cast<size_t>(std::clamp(std::round(
            (s.tank_kwh-p.tank_min_kwh)/forward_tank_bin_kwh),0.0,double(tank_count-1)));
        return r*tank_count+w;
    };
    struct Node { State state; double cost; uint16_t parent; uint8_t choice; };
    struct Trace { uint16_t parent; uint8_t choice; };
    std::vector<Trace> traces;
    traces.reserve(count*grid_size);
    std::array<size_t,max_periods> offsets{};
    std::vector<Node> frontier{{initial,0,0,255}};
    std::vector<Node> best(grid_size);
    std::vector<double> rank(grid_size);
    for (size_t t=0;t<count;++t) {
        std::fill(rank.begin(),rank.end(),infinity);
        for (auto& node:best) node.choice=255;
        const auto options=actions(p,periods[t].outdoor_c);
        for (size_t i=0;i<frontier.size();++i) {
            for (uint8_t a=0;a<options.size();++a) {
                const Step step=advance(p,frontier[i].state,periods[t],options[a]);
                if (!feasible(p,step.next)) continue;
                if (t+1==count && (step.next.indoor_c < initial.indoor_c-1e-7 ||
                                   step.next.tank_kwh < initial.tank_kwh-1e-7)) continue;
                const double cost=frontier[i].cost+step.cost_eur;
                const size_t k=bin(step.next);
                // Preserve more stored heat within a bin when its small extra
                // cost is below a conservative future replacement value.
                const double stored=(step.next.indoor_c-p.indoor_min_c)*
                    p.building_capacity_kwh_per_c+step.next.tank_kwh-p.tank_min_kwh;
                const double score=cost-0.05*stored;
                if (score < rank[k]) {
                    rank[k]=score;
                    best[k]={step.next,cost,static_cast<uint16_t>(i),a};
                }
            }
        }
        std::vector<Node> next;
        next.reserve(grid_size);
        offsets[t]=traces.size();
        for (const auto& node:best) if (node.choice!=255) {
            next.push_back(node);
            traces.push_back({node.parent,node.choice});
        }
        if (next.empty()) { result.status=OptimizeStatus::Infeasible; return result; }
        frontier.swap(next);
    }
    size_t selected=0;
    for (size_t i=1;i<frontier.size();++i)
        if (frontier[i].cost<frontier[selected].cost) selected=i;
    result.cost_eur=frontier[selected].cost;
    result.count=count;
    result.status=OptimizeStatus::Ok;
    for (size_t t=count;t>0;--t) {
        const Trace trace=traces[offsets[t-1]+selected];
        result.actions[t-1]=actions(p,periods[t-1].outdoor_c)[trace.choice];
        selected=trace.parent;
    }
    return result;
}
}

OptimizeResult optimize(const Parameters& p, State initial,
                        const Period* periods, size_t count, size_t max_grid_states) {
    OptimizeResult result{};
    if (!valid(p) || !feasible(p, initial) || !periods || count == 0 ||
        count > max_periods || max_grid_states == 0) return result;
    for (size_t t = 0; t < count; ++t) {
        const Period& x = periods[t];
        if (!std::isfinite(x.outdoor_c) || !std::isfinite(x.total_price_eur_per_kwh) ||
            !std::isfinite(x.internal_gain_kw) ||
            !std::isfinite(x.dhw_draw_kw) || x.dhw_draw_kw < 0) return result;
    }
    const size_t room_count = static_cast<size_t>(std::ceil(
        (p.indoor_max_c - p.indoor_min_c) / room_bin_c)) + 1;
    const size_t tank_count = static_cast<size_t>(std::ceil(
        (p.tank_capacity_kwh - p.tank_min_kwh) / tank_bin_kwh)) + 1;
    if (room_count == 0 || tank_count == 0 || room_count > max_grid_states /
        tank_count) { result.status = OptimizeStatus::ResourceLimit; return result; }
    const size_t grid_size = room_count * tank_count;
    auto index = [tank_count](size_t room, size_t tank) { return room * tank_count + tank; };
    auto discretize = [&](State s) {
        const auto room = static_cast<size_t>(std::clamp(
            std::round((s.indoor_c - p.indoor_min_c) / room_bin_c),
            0.0, static_cast<double>(room_count - 1)));
        const auto tank = static_cast<size_t>(std::clamp(
            std::round((s.tank_kwh - p.tank_min_kwh) / tank_bin_kwh),
            0.0, static_cast<double>(tank_count - 1)));
        return index(room, tank);
    };
    // Policy is one byte per state and period; with the grid cap, <= 1.5 MiB.
    std::vector<uint8_t> policy(count * grid_size, 255);
    std::vector<double> next(grid_size, infinity), current(grid_size, infinity);
    for (size_t r = 0; r < room_count; ++r) {
        for (size_t w = 0; w < tank_count; ++w) {
            State s{p.indoor_min_c + r * room_bin_c,
                    p.tank_min_kwh + w * tank_bin_kwh};
            if (feasible(p, s) && s.indoor_c >= initial.indoor_c + room_bin_c - 1e-7 &&
                s.tank_kwh >= initial.tank_kwh + tank_bin_kwh - 1e-7) next[index(r,w)] = 0;
        }
    }
    for (size_t reverse = count; reverse-- > 0;) {
        std::fill(current.begin(), current.end(), infinity);
        const auto choices = actions(p, periods[reverse].outdoor_c);
        for (size_t r = 0; r < room_count; ++r) {
            for (size_t w = 0; w < tank_count; ++w) {
                State s{p.indoor_min_c + r * room_bin_c,
                        p.tank_min_kwh + w * tank_bin_kwh};
                if (!feasible(p, s)) continue;
                const size_t here = index(r,w);
                for (uint8_t a = 0; a < choices.size(); ++a) {
                    const Step step = advance(p, s, periods[reverse], choices[a]);
                    if (!feasible(p, step.next)) continue;
                    const double tail = next[discretize(step.next)];
                    const double value = step.cost_eur + tail;
                    if (value < current[here]) {
                        current[here] = value;
                        policy[reverse * grid_size + here] = a;
                    }
                }
            }
        }
        current.swap(next);
    }
    if (!std::isfinite(next[discretize(initial)])) {
        return forward_exact(p,initial,periods,count,max_grid_states);
    }
    State actual = initial;
    for (size_t t = 0; t < count; ++t) {
        const uint8_t choice = policy[t * grid_size + discretize(actual)];
        if (choice == 255) return forward_exact(p,initial,periods,count,max_grid_states);
        const auto selected = actions(p, periods[t].outdoor_c)[choice];
        const Step step = advance(p, actual, periods[t], selected);
        if (!feasible(p, step.next)) {
            return forward_exact(p,initial,periods,count,max_grid_states);
        }
        result.actions[t] = selected;
        result.cost_eur += step.cost_eur;
        actual = step.next;
    }
    if (actual.indoor_c < initial.indoor_c - 1e-7 ||
        actual.tank_kwh < initial.tank_kwh - 1e-7) {
        return forward_exact(p,initial,periods,count,max_grid_states);
    }
    result.status = OptimizeStatus::Ok;
    result.count = count;
    const auto exact = forward_exact(p,initial,periods,count,max_grid_states);
    if (exact.status == OptimizeStatus::Ok && exact.cost_eur < result.cost_eur)
        return exact;
    return result;
}
}
