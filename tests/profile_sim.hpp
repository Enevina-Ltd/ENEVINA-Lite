#pragma once
// SIMULATION harness for optimization profiles. Real recorded Nord Pool prices,
// SYNTHETIC building, weather and heat pump (an idealized thermostat pump using
// the generic cottage capacity/COP curves). Results are simulated cost ratios
// under stated assumptions, NOT measured savings and not a Mitsubishi model.
#include "cottage/optimization_profile.hpp"
#include "cottage/physics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace profile_sim {
using namespace enevina::cottage;

struct Series {
    std::vector<long long> utc;
    std::vector<double> per_mwh;     // currency per MWh, zone currency
};
inline bool load_series(const std::string& path,Series& out) {
    std::FILE* f=std::fopen(path.c_str(),"rb");
    if(!f) return false;
    char line[128];
    bool header=true;
    while(std::fgets(line,sizeof(line),f)) {
        if(header) {header=false;continue;}
        long long t=0;double p=0;
        if(std::sscanf(line,"%lld,%lf",&t,&p)!=2) {std::fclose(f);return false;}
        out.utc.push_back(t);out.per_mwh.push_back(p);
    }
    std::fclose(f);
    return out.utc.size()>=200;
}

struct Building {
    double mass_kwh_per_c=6.0;
    double loss_kw_per_c=0.12;
    double target_c=21.0;
    double start_c=21.0;
};
struct Outcome {
    double cost=0,kwh=0,min_c=1e9,max_c=-1e9;
    int minutes_outside_limits=0;
    int switches=0;
    int heating_slots=0;
    int slots=0;
    double final_c=std::numeric_limits<double>::quiet_NaN();
};

inline Parameters pump() {
    Parameters p{};
    p.heat_kw_at_zero_c=3.2;p.heat_kw_change_per_outdoor_c=0.03;p.minimum_heat_kw=1.0;
    p.cop_at_zero_c=2.8;p.cop_change_per_outdoor_c=0.04;p.minimum_cop=1.2;p.maximum_cop=5.0;
    p.thermal_mass_kwh_per_c=2.0;p.loss_kw_per_c=0.12;
    return p;
}
// Synthetic outdoor temperature: -3 C mean, +-4 C daily swing, slow weekly drift.
inline double outdoor_c(size_t slot) {
    const double h=static_cast<double>(slot)/4.0;
    return -3.0+4.0*std::sin(6.283185307179586*(h-9.0)/24.0)+
           2.0*std::sin(6.283185307179586*h/(24.0*7.0));
}

// use_prices=false: the controller sees no prices (plain thermostat baseline).
inline Outcome simulate(const Series& s,double addon_per_kwh,OptimizationProfile profile,
                        double lo,double hi,bool use_prices,const Building& b={}) {
    Outcome o;
    const Parameters pm=pump();
    const size_t n=s.per_mwh.size(),tail=16;
    double T=b.start_c,setpoint=b.target_c;
    bool on=false;
    for(size_t t=0;t+tail<n;++t) {
        BootstrapInput in{};in.heating={16,26,0.5,true,true};
        in.indoor_fresh=true;in.indoor_c=T;in.desired_room_c=b.target_c;
        size_t window=8;
        double totals[16];
        if(!apply_profile(in,profile,lo,hi,0.10,window)) std::abort();
        for(size_t i=0;i<window;++i) totals[i]=s.per_mwh[t+i]/1000.0+addon_per_kwh;
        in.next_total_prices=totals;in.price_count=window;
        in.prices_contiguous_and_fresh=use_prices;
        const BootstrapDecision d=bootstrap_price_decision(in);
        bool next_on=on;
        if(d.action==BootstrapAction::Off) next_on=false;
        else if(d.action==BootstrapAction::HeatSetpoint) {next_on=true;setpoint=d.heat_setpoint_c;}
        if(next_on!=on) ++o.switches;
        on=next_on;
        const double out=outdoor_c(t);
        double q=0;
        if(on) {
            // Idealized pump thermostat: delivers what is needed to reach its
            // setpoint, limited by capacity at this outdoor temperature.
            const double room_setpoint=std::min(setpoint,hi>0?hi+1.0:b.target_c+2.0);
            const double need=(room_setpoint-T)*b.mass_kwh_per_c/slot_hours+b.loss_kw_per_c*(T-out);
            q=std::max(0.0,std::min(need,heat_capacity_kw(pm,out)));
            ++o.heating_slots;
        }
        const double electric=q>0?q*slot_hours/heat_cop(pm,out)+0.012*slot_hours:0.002*slot_hours;
        o.kwh+=electric;
        o.cost+=electric*totals[0];
        T+=(q-b.loss_kw_per_c*(T-out))*slot_hours/b.mass_kwh_per_c;
        o.min_c=std::min(o.min_c,T);o.max_c=std::max(o.max_c,T);
        const double eff_lo=lo>0?lo:b.target_c-0.5,eff_hi=hi>0?hi:b.target_c+0.5;
        // A little numerical slack: limits are decided on the previous slot's reading.
        if(T<eff_lo-0.5||T>eff_hi+0.5) o.minutes_outside_limits+=15;
        ++o.slots;
    }
    o.final_c=T;
    return o;
}

// Equal-terminal thermal-energy accounting, NOT an instant physical heating
// command or a measured saving. Charge every run for the heat it would need
// to finish with the SAME stored building energy as the warmest run.
// The final recorded 4-hour market average and the model's terminal COP
// provide a common energy-equivalent recovery price. Actual time/dwell
// feasibility of that hypothetical recovery is NOT demonstrated here.
struct TerminalAdjustment {
    double target_c=0, thermal_kwh=0, electric_kwh=0, cost=0, equalized_cost=0;
};
inline TerminalAdjustment equalize_terminal(const Outcome& run,
                                             const Building& building,
                                             double common_c,double recovery_price,
                                             double recovery_cop) {
    TerminalAdjustment o{};
    o.target_c=common_c;
    if(!std::isfinite(run.final_c) || !std::isfinite(run.cost) ||
       !std::isfinite(common_c) || common_c+1e-9<run.final_c ||
       !std::isfinite(recovery_price) || !std::isfinite(recovery_cop) ||
       recovery_cop<1 || building.mass_kwh_per_c<=0) {
        o.equalized_cost=std::numeric_limits<double>::quiet_NaN();
        return o;
    }
    o.thermal_kwh=std::max(0.0,common_c-run.final_c)*building.mass_kwh_per_c;
    o.electric_kwh=o.thermal_kwh/recovery_cop;
    o.cost=o.electric_kwh*recovery_price;
    o.equalized_cost=run.cost+o.cost;
    return o;
}
} // namespace profile_sim
