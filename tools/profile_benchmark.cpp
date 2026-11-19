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
    std::printf("| Zone (7 days) | Profile | Raw cost | End C | Heat debt kWh | Equalized cost | vs equalized thermostat |\n");
    std::printf("|---|---|---:|---:|---:|---:|---:|\n");
    const profile_sim::Building building{};
    for(const auto& z:zones) {
        profile_sim::Series series;
        const std::string path=std::string(argv[1])+"/"+z.file;
        if(!profile_sim::load_series(path,series)) {std::fprintf(stderr,"cannot read %s\n",path.c_str());return 1;}
        const auto baseline=profile_sim::simulate(series,z.addon,enevina::cottage::OptimizationProfile::Comfort,19,23,false,building);
        const auto comfort=profile_sim::simulate(series,z.addon,enevina::cottage::OptimizationProfile::Comfort,19,23,true,building);
        const auto balanced=profile_sim::simulate(series,z.addon,enevina::cottage::OptimizationProfile::Balanced,19,23,true,building);
        const auto saving=profile_sim::simulate(series,z.addon,enevina::cottage::OptimizationProfile::Saving,19,23,true,building);
        const profile_sim::Outcome runs[]={baseline,comfort,balanced,saving};
        const char* names[]={"thermostat (no prices)","comfort","balanced","saving"};
        double terminal=baseline.final_c;
        for(const auto& run:runs)terminal=std::max(terminal,run.final_c);
        double end_price=0;
        for(size_t i=series.per_mwh.size()-16;i<series.per_mwh.size();++i)
            end_price+=series.per_mwh[i]/1000.0+z.addon;
        end_price/=16;
        const auto end_cop=enevina::cottage::heat_cop(profile_sim::pump(),
            profile_sim::outdoor_c(series.per_mwh.size()-1));
        const auto base_adj=profile_sim::equalize_terminal(baseline,building,terminal,end_price,end_cop);
        for(size_t i=0;i<4;++i) {
            const auto adj=profile_sim::equalize_terminal(runs[i],building,terminal,end_price,end_cop);
            std::printf("| %s | %s | %.2f %s | %.2f | %.2f | %.2f %s | %+.1f %% |\n",
                z.label,names[i],runs[i].cost,z.cur,runs[i].final_c,adj.thermal_kwh,
                adj.equalized_cost,z.cur,100*(adj.equalized_cost/base_adj.equalized_cost-1));
        }
    }
    return 0;
}
