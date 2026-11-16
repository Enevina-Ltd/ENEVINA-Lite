#include "cottage/mpc.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace enevina::cottage;
int main(int argc,char**argv) {
    if(argc!=3) {
        std::fprintf(stderr,"usage: cottage_cli {mild|cold|price_spike|defrost} {96|192}\n");
        return 2;
    }
    const int n=std::atoi(argv[2]);
    if(n!=96 && n!=192) return 2;
    const char* name=argv[1];
    if(std::strcmp(name,"mild") && std::strcmp(name,"cold") &&
       std::strcmp(name,"price_spike") && std::strcmp(name,"defrost")) return 2;
    Parameters p{};
    p.thermal_mass_kwh_per_c=2.0;
    p.loss_kw_per_c=0.12;
    p.heat_kw_at_zero_c=3.2;
    p.heat_kw_change_per_outdoor_c=0.03;
    p.minimum_heat_kw=1.0;
    p.cop_at_zero_c=2.8;
    p.cop_change_per_outdoor_c=0.04;
    p.minimum_cop=1.2;
    p.maximum_cop=5;
    p.startup_delay_minutes=3;
    p.startup_electric_kwh=0.05;
    p.heat_standby_kw=0.012;
    p.off_standby_kw=0.002;
    p.planning_min_c=8.0;
    p.critical_c=5.0;
    p.maximum_indoor_c=14.5;
    p.switching_penalty_eur=0.03;
    p.min_heat_minutes=60;
    p.min_off_minutes=60;
    const State initial{10.0,false,180};
    std::vector<Period> slots(n);
    for(int i=0;i<n;++i) {
        const double baseline=-4+2*std::sin(6.283185307179586*i/96);
        const double outside=std::strcmp(name,"cold")==0 ? baseline-6 : baseline;
        double price=(i%96<20 || i%96>=80)?0.07:0.41;
        if(std::strcmp(name,"price_spike")==0 && (i%96>=24 && i%96<72))
            price=0.95;
        double defrost=1.0;
        if(std::strcmp(name,"defrost")==0 && i%32>=12 && i%32<17)
            defrost=0.65;
        slots[i]={outside,price,0,defrost};
    }
    const auto result=optimize(p,initial,slots.data(),slots.size());
    std::printf("{\"case\":\"%s\",\"periods\":%d,\"status\":%d,"
        "\"cost_eur\":%.10f,\"electric_kwh\":%.10f,\"min_c\":%.10f,"
        "\"final_c\":%.10f,\"starts\":%u,"
        "\"initial_c\":%.10f,\"initial_on\":false,"
        "\"initial_minutes\":%u,\"mass\":%.10f,\"loss\":%.10f,"
        "\"heat_at_zero\":%.10f,\"heat_slope\":%.10f,\"min_heat_kw\":%.10f,"
        "\"cop_at_zero\":%.10f,\"cop_slope\":%.10f,\"min_cop\":%.10f,\"max_cop\":%.10f,"
        "\"heat_standby_kw\":%.10f,\"off_standby_kw\":%.10f,"
        "\"startup_kwh\":%.10f,\"startup_delay_min\":%.10f,"
        "\"switch_penalty\":%.10f,\"min_heat_min\":%u,\"min_off_min\":%u,"
        "\"minimum_c\":%.10f,\"maximum_c\":%.10f,\"slots\":[",
        name,n,static_cast<int>(result.status),
        result.cost_eur,result.electric_kwh,result.minimum_c,result.final_c,result.starts,
        initial.indoor_c,initial.minutes_in_mode,p.thermal_mass_kwh_per_c,p.loss_kw_per_c,
        p.heat_kw_at_zero_c,p.heat_kw_change_per_outdoor_c,p.minimum_heat_kw,
        p.cop_at_zero_c,p.cop_change_per_outdoor_c,p.minimum_cop,p.maximum_cop,
        p.heat_standby_kw,p.off_standby_kw,
        p.startup_electric_kwh,p.startup_delay_minutes,p.switching_penalty_eur,
        p.min_heat_minutes,p.min_off_minutes,p.planning_min_c,p.maximum_indoor_c);
    for(int i=0;i<n;++i) {
        const auto& d=slots[i];
        std::printf("%s[%.10f,%.10f,%.10f,%.10f,%u]",i?",":"",
            d.outdoor_c,d.total_price_eur_per_kwh,
            d.internal_gain_kw,d.defrost_capacity_factor,
            static_cast<unsigned>(result.heat_16_c[i]));
    }
    std::puts("]}");
    return result.status==MpcStatus::Ok?0:1;
}
