#include "cottage/mpc.hpp"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <vector>
// Always-on safety checks: -DNDEBUG must not bypass validation.
#define check(expression) do { \
    if(!(expression)) { \
        std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#expression); \
        std::exit(1); \
    } \
} while(false)
using namespace enevina::cottage;
int main() {
    // Codex reproduction: all-HEAT is valid, but a cheaper trajectory with
    // one OFF slot can evict it from a 0.1 C state bin.
    Parameters p{};
    p.thermal_mass_kwh_per_c=20;
    p.loss_kw_per_c=0.12;
    p.heat_kw_at_zero_c=1.81;
    p.heat_kw_change_per_outdoor_c=0;
    p.minimum_heat_kw=1.81;
    p.cop_at_zero_c=2.8;
    p.minimum_cop=1.2;
    p.maximum_cop=5;
    p.heat_standby_kw=0.012;
    p.off_standby_kw=0.002;
    p.startup_delay_minutes=3;
    p.startup_electric_kwh=0.05;
    p.planning_min_c=8;
    p.critical_c=5;
    p.maximum_indoor_c=15.8;
    p.switching_penalty_eur=0.02;
    p.min_heat_minutes=60;
    p.min_off_minutes=30;
    const State initial{10.0,true,180};
    const std::vector<Period> periods(96,{-5,0.3,0,1});
    check(valid(p));
    State all_heat=initial;
    for(const auto& period:periods) {
        const auto step=advance(p,all_heat,period,true);
        all_heat=step.next;
        check(all_heat.indoor_c>=p.planning_min_c &&
               all_heat.indoor_c<=p.maximum_indoor_c);
    }
    check(all_heat.indoor_c>=initial.indoor_c);
    check(std::abs(all_heat.indoor_c-10.011176)<0.0001);
    const auto solution=optimize(p,initial,periods.data(),periods.size());
    check(solution.status==MpcStatus::Ok && solution.count==periods.size());
    State exact=initial;
    for(size_t i=0;i<solution.count;++i) {
        exact=advance(p,exact,periods[i],solution.heat_16_c[i]!=0).next;
        check(std::isfinite(exact.indoor_c));
        check(exact.indoor_c>=p.planning_min_c-1e-8);
        check(exact.indoor_c<=p.maximum_indoor_c+1e-8);
    }
    check(exact.indoor_c>=initial.indoor_c-1e-8);
    check(std::abs(exact.indoor_c-solution.final_c)<1e-7);
    std::puts("PASS MPC retains terminal-feasible heating after grid bin merges");
}
