// Closed-loop regression guard (SIMULATION): thermostat vs Lite price control
// vs calibrated MPC on identical buildings, weather, prices and initial
// states. Asserts the safety/honesty properties and pins the cases where the
// MPC does NOT help, so a behaviour change is noticed in review.
#include "tests/cottage_closed_loop.hpp"
#include <cstdio>
#include <string>

using namespace closed_loop;

namespace {
int failures=0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); ++failures; } } while(0)
unsigned fallback(const Metrics& m,const char* why) {
    const auto it=m.fallbacks.find(why);
    return it==m.fallbacks.end()?0:it->second;
}
}

int main() {
    for(const auto& s:scenarios()) {
        Harness h(s);
        const Metrics t=h.run(Kind::Thermostat);
        const Metrics p=h.run(Kind::Price);
        const Metrics m=h.run(Kind::Mpc);
        std::printf("%-30s thermo %.3f price %.3f mpc %.3f EUR(eq) | below K*h %.2f %.2f %.2f | "
                    "starts %u %u %u | mpc %u/%zu fails %u max %.1f ms\n",
            s.name.c_str(),t.equalized_cost,p.equalized_cost,m.equalized_cost,
            t.below_kh,p.below_kh,m.below_kh,t.starts,p.starts,m.starts,
            m.mpc_active,s.eval_slots,m.solver_failures,m.solve_ms_max);
        // Comfort: the MPC pipeline is never worse than the better baseline
        // by more than 0.1 K*h, in every scenario.
        CHECK(m.below_kh<=std::min(t.below_kh,p.below_kh)+0.1);
        CHECK(m.above_kh<=std::max(t.above_kh,p.above_kh)+0.1);
        // Learning never invents a model it cannot validate.
        if(s.name=="undersized_pump") {
            CHECK(m.mpc_active==0);
            CHECK(fallback(m,"MODEL_LEARNING")==s.eval_slots);
        } else {
            CHECK(m.mpc_active>0);
        }
        if(s.name=="sensor_outage") CHECK(fallback(m,"TEMPERATURE_STALE")==24);
        if(s.name=="weather_price_outage") CHECK(fallback(m,"FORECAST_UNAVAILABLE")>=48);
        CHECK(m.invalid_learned_range_plans==0);
        if(s.name=="cold_spell") {
            CHECK(m.learned_range_limited_plans>0);
            // A supported prefix replaces a needless full-horizon fallback.
            // The separate planner regression refuses a <8 h prefix.
        }
        // Price-feature scenarios: the MPC is cheaper at equal final energy.
        if(s.name=="price_spike" || s.name=="cheap_night_expensive_morning" ||
           s.name=="negative_prices" || s.name=="price_spike_pump_meter")
            CHECK(m.equalized_cost<p.equalized_cost);
        // Where the MPC does not help: flat prices (no price signal; model
        // error margin costs energy). Pinned so it stays visible.
        if(s.name=="flat_prices") {
            CHECK(m.equalized_cost>=p.equalized_cost);
            CHECK(m.equalized_cost<=p.equalized_cost*1.03);
        }
        CHECK(m.solve_ms_max<2000.0);
    }
    if(failures) {std::printf("%d closed-loop check(s) failed\n",failures);return 1;}
    std::puts("cottage_closed_loop_tests: all passed");
    return 0;
}
