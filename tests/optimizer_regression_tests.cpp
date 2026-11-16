#include "optimizer/optimizer.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace enevina;
static void check(bool ok,const char* why) {
    if(!ok) {std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
static Parameters parameters() {
    Parameters p{};
    p.building_capacity_kwh_per_c=10;
    p.building_loss_kw_per_c=0;
    p.tank_capacity_kwh=8;
    p.tank_loss_kw_per_kwh=0.002;
    p.pump_max_thermal_kw=8;
    p.cop_at_reference=3;
    p.cop_outdoor_per_c=0.05;
    p.cop_supply_per_c=0.03;
    p.minimum_cop=1;
    p.maximum_cop=6;
    p.emitter_kw_per_c=1;
    p.dhw_supply_c=50;
    p.indoor_min_c=19;
    p.indoor_max_c=22;
    p.tank_min_kwh=2;
    return p;
}
int main() {
    auto p=parameters();
    const Period step{0,0.30,0,3.99,0};
    for(double initial_tank : {5.001,5.00095,5.00105,5.002}) {
        const State start{20,initial_tank};
        const auto half=advance(p,start,step,{Mode::Dhw,4,50});
        const auto full=advance(p,start,step,{Mode::Dhw,8,50});
        check(half.next.tank_kwh<start.tank_kwh-1e-7,
              "half-power terminal violates exact constraint");
        check(full.next.tank_kwh>start.tank_kwh,"full-power should be feasible");
        const auto got=optimize(p,start,&step,1);
        check(got.status==OptimizeStatus::Ok,
              "an exact feasible terminal must not be hidden by a cheaper rejected candidate");
        check(got.count==1 && got.actions[0].mode==Mode::Dhw &&
              got.actions[0].requested_thermal_kw==8,
              "next exact-feasible terminal must be chosen");
        const auto verified=advance(p,start,step,got.actions[0]);
        check(feasible(p,verified.next) &&
              verified.next.tank_kwh>=start.tank_kwh-1e-7,
              "returned plan must pass independent double-precision replay");
        check(std::abs(verified.cost_eur-got.cost_eur)<1e-9,
              "verified cost must be exact");
    }
    p.tank_loss_kw_per_kwh=0;
    const auto start=State{20,5.001};
    const auto got=optimize(p,start,&step,1);
    check(got.status==OptimizeStatus::Ok &&
          got.actions[0].requested_thermal_kw==4,
          "a truly feasible cheaper half-power trajectory must remain preferred");

    // Codex: terminal-state ranking must not sacrifice free idle to retain
    // needless stored heat when no future demand remains.
    auto free_p=parameters();
    free_p.building_capacity_kwh_per_c=50;
    free_p.tank_loss_kw_per_kwh=0;
    const State idle_start{20.03,5.0};
    const Period idle_period{0,0.10,0,0,0};
    const auto idle_result=optimize(free_p,idle_start,&idle_period,1);
    check(idle_result.status==OptimizeStatus::Ok &&
          idle_result.actions[0].mode==Mode::Idle &&
          std::abs(idle_result.cost_eur)<1e-10,
          "idle must beat costly heat when terminal constraints already pass");
    // Bound work deterministically; no sleep or platform clock dependency.
    OptimizeRunControls tiny{};
    tiny.max_work_units=1;
    const auto limited=optimize(p,start,&step,1,8192,tiny);
    check(limited.status==OptimizeStatus::WorkLimit &&
          limited.count==0,"work cap must stop search without partial output");
    bool cancelled=true;
    auto cancel_bool=[](void* ptr)->bool {return *static_cast<bool*>(ptr);};
    OptimizeRunControls stop{};
    stop.cancel_requested=cancel_bool;
    stop.cancel_context=&cancelled;
    const auto before=optimize(p,start,&step,1,8192,stop);
    check(before.status==OptimizeStatus::Cancelled && before.count==0,
          "pre-search cancellation must fail closed");
    struct CancelCounter {size_t calls=0;size_t limit=0;};
    CancelCounter counter{0,100};
    auto cancel_after=[](void* ptr)->bool {
        auto& c=*static_cast<CancelCounter*>(ptr);
        return ++c.calls>=c.limit;
    };
    stop.cancel_requested=cancel_after;
    stop.cancel_context=&counter;
    const auto middle=optimize(p,start,&step,1,8192,stop);
    check(middle.status==OptimizeStatus::Cancelled &&
          middle.count==0 && counter.calls>=counter.limit,
          "expansion loop must poll cancellation callback");
    stop={};
    const auto recovered=optimize(p,start,&step,1,8192,stop);
    check(recovered.status==OptimizeStatus::Ok,
          "normal optimization must recover after cancellation");
    std::puts("PASS optimizer terminal candidate fallback and boundary regressions");
}
