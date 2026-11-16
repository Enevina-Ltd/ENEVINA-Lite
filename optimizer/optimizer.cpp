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

// Float search states are merged into bounded bins. The chosen trajectory is
// checked again using the double-precision physical model before acceptance.
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
        room_count*tank_count > 800000/count ||
        room_count*tank_count > UINT16_MAX) {
        result.status=OptimizeStatus::ResourceLimit; return result;
    }
    const size_t grid_size=room_count*tank_count;
    struct SearchState { float indoor_c; float tank_kwh; };
    const float room_bin=static_cast<float>(forward_room_bin_c);
    const float tank_bin=static_cast<float>(forward_tank_bin_kwh);
    const float minimum_room=static_cast<float>(p.indoor_min_c);
    const float maximum_room=static_cast<float>(p.indoor_max_c);
    const float minimum_tank=static_cast<float>(p.tank_min_kwh);
    const float maximum_tank=static_cast<float>(p.tank_capacity_kwh);
    auto bin = [&](SearchState s) {
        const size_t r=static_cast<size_t>(std::clamp(
            (s.indoor_c-minimum_room)/room_bin+0.5f,0.0f,float(room_count-1)));
        const size_t w=static_cast<size_t>(std::clamp(
            (s.tank_kwh-minimum_tank)/tank_bin+0.5f,0.0f,float(tank_count-1)));
        return r*tank_count+w;
    };
    struct Node { SearchState state; float cost; uint16_t parent; uint8_t choice; };
    struct Trace { uint16_t parent; uint8_t choice; };
    std::vector<Trace> traces;
    traces.reserve(count*grid_size);
    std::array<size_t,max_periods> offsets{};
    std::vector<Node> frontier{{{static_cast<float>(initial.indoor_c),
        static_cast<float>(initial.tank_kwh)},0,0,255}};
    std::vector<Node> best(grid_size);
    std::vector<float> rank(grid_size);
    for (size_t t=0;t<count;++t) {
        std::fill(rank.begin(),rank.end(),infinity);
        for (auto& node:best) node.choice=255;
        const auto options=actions(p,periods[t].outdoor_c);
        struct Effect { float space_kw; float dhw_kw; float cost_eur; };
        std::array<Effect,7> effects{};
        const double maximum=available_thermal_kw(p,periods[t].outdoor_c);
        const double reference_indoor=(p.indoor_min_c+p.indoor_max_c)/2;
        for(size_t a=0;a<options.size();++a) {
            const Action& option=options[a];
            double delivered=option.mode==Mode::Idle ? 0 :
                std::min(std::max(0.0,option.requested_thermal_kw),maximum);
            if(option.mode==Mode::Space)
                delivered=std::min(delivered,p.emitter_kw_per_c*
                    std::max(0.0,option.supply_c-reference_indoor));
            const double electric=delivered>0 ?
                delivered*step_hours/cop(p,periods[t].outdoor_c,option.supply_c) : 0;
            effects[a]={static_cast<float>(option.mode==Mode::Space ? delivered : 0),
                static_cast<float>(option.mode==Mode::Dhw ? delivered : 0),
                static_cast<float>(electric*periods[t].total_price_eur_per_kwh)};
        }
        const float room_gain=static_cast<float>(periods[t].internal_gain_kw);
        const float room_outdoor=static_cast<float>(periods[t].outdoor_c);
        const float dhw_draw=static_cast<float>(periods[t].dhw_draw_kw);
        const float room_loss=static_cast<float>(p.building_loss_kw_per_c);
        const float room_capacity=static_cast<float>(p.building_capacity_kwh_per_c);
        const float tank_loss=static_cast<float>(p.tank_loss_kw_per_kwh);
        const float initial_room=static_cast<float>(initial.indoor_c);
        const float initial_tank=static_cast<float>(initial.tank_kwh);
        for (size_t i=0;i<frontier.size();++i) {
            for (uint8_t a=0;a<options.size();++a) {
                const Effect& effect=effects[a];
                const SearchState& state=frontier[i].state;
                const SearchState next_state{
                    state.indoor_c+0.25f*(effect.space_kw+room_gain-
                        room_loss*(state.indoor_c-room_outdoor))/room_capacity,
                    state.tank_kwh+0.25f*(effect.dhw_kw-dhw_draw-
                        tank_loss*state.tank_kwh)};
                if (!(next_state.indoor_c>=minimum_room-1e-5f &&
                      next_state.indoor_c<=maximum_room+1e-5f &&
                      next_state.tank_kwh>=minimum_tank-1e-5f &&
                      next_state.tank_kwh<=maximum_tank+1e-5f)) continue;
                if (t+1==count && (next_state.indoor_c < initial_room-1e-5f ||
                                   next_state.tank_kwh < initial_tank-1e-5f)) continue;
                const float cost=frontier[i].cost+effect.cost_eur;
                const size_t k=bin(next_state);
                // Preserve more stored heat within a bin when its small extra
                // cost is below a conservative future replacement value.
                const float stored=(next_state.indoor_c-minimum_room)*
                    room_capacity+next_state.tank_kwh-minimum_tank;
                const float score=cost-0.05f*stored;
                if (score < rank[k]) {
                    rank[k]=score;
                    best[k]={next_state,cost,static_cast<uint16_t>(i),a};
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
    // Sort only the final frontier in-place: no terminal-index allocation.
    // Earlier trace layers retain their original indices, and each final Node
    // already carries its own predecessor and action.
    std::sort(frontier.begin(),frontier.end(),[](const Node& a,const Node& b) {
        if(a.cost!=b.cost) return a.cost<b.cost;
        if(a.parent!=b.parent) return a.parent<b.parent;
        return a.choice<b.choice;
    });
    for(const Node& terminal:frontier) {
        OptimizeResult candidate{};
        candidate.count=count;
        candidate.actions[count-1]=actions(p,periods[count-1].outdoor_c)[terminal.choice];
        size_t selected=terminal.parent;
        for(size_t t=count-1;t>0;--t) {
            const Trace trace=traces[offsets[t-1]+selected];
            candidate.actions[t-1]=actions(p,periods[t-1].outdoor_c)[trace.choice];
            selected=trace.parent;
        }
        State verified=initial;
        double verified_cost=0;
        bool exact_feasible=true;
        for(size_t t=0;t<count;++t) {
            const Step step=advance(p,verified,periods[t],candidate.actions[t]);
            if(!feasible(p,step.next)) {
                exact_feasible=false;
                break;
            }
            verified=step.next;
            verified_cost+=step.cost_eur;
        }
        if(!exact_feasible || verified.indoor_c<initial.indoor_c-1e-7 ||
           verified.tank_kwh<initial.tank_kwh-1e-7)
            continue;
        candidate.status=OptimizeStatus::Ok;
        candidate.cost_eur=verified_cost;
        return candidate;
    }
    result.status=OptimizeStatus::Infeasible;
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
    return forward_exact(p,initial,periods,count,max_grid_states);
}
}
