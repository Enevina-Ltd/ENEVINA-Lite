// Prints the simulated profile comparison on REAL recorded prices as Markdown.
// usage: profile_benchmark <tests/data/prices dir>
#include "../tests/profile_sim.hpp"
#include <cstdio>

int main(int argc,char** argv) {
    if(argc!=2) {std::fprintf(stderr,"usage: profile_benchmark <prices dir>\n");return 2;}
    struct Zone {const char* file;const char* label;const char* cur;double addon;};
    const Zone zones[]={
        {"FI_EUR_2026-11-12_to_18.csv","Finland FI","EUR",0.06},
        {"DK1_EUR_2026-11-12_to_18.csv","Denmark DK1","EUR",0.06},
        {"SE3_SEK_2026-11-12_to_18.csv","Sweden SE3","SEK",0.70}};
    std::printf("| Zone (7 days, 15-min real prices) | Profile | Cost | kWh | Cost vs thermostat | Heating share | Mode switches | Room min..max C |\n");
    std::printf("|---|---|---:|---:|---:|---:|---:|---|\n");
    for(const auto& z:zones) {
        profile_sim::Series s;
        const std::string path=std::string(argv[1])+"/"+z.file;
        if(!profile_sim::load_series(path,s)) {std::fprintf(stderr,"cannot read %s\n",path.c_str());return 1;}
        const auto base=profile_sim::simulate(s,z.addon,enevina::cottage::OptimizationProfile::Comfort,19,23,false);
        std::printf("| %s | thermostat (no prices) | %.2f %s | %.1f | 0 %% | %.0f %% | %d | %.2f..%.2f |\n",
            z.label,base.cost,z.cur,base.kwh,100.0*base.heating_slots/base.slots,base.switches,base.min_c,base.max_c);
        for(auto p:{enevina::cottage::OptimizationProfile::Comfort,enevina::cottage::OptimizationProfile::Balanced,
                    enevina::cottage::OptimizationProfile::Saving}) {
            const auto r=profile_sim::simulate(s,z.addon,p,19,23,true);
            std::printf("| %s | %s | %.2f %s | %.1f | %+.1f %% | %.0f %% | %d | %.2f..%.2f |\n",z.label,
                enevina::cottage::profile_name(p),r.cost,z.cur,r.kwh,100.0*(r.cost/base.cost-1.0),
                100.0*r.heating_slots/r.slots,r.switches,r.min_c,r.max_c);
        }
    }
    return 0;
}
