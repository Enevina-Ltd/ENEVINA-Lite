// Prints the /api/timeline JSON for a slice of recorded prices, using the SAME
// pipeline and JSON writer as the firmware (cottage/plan_timeline.hpp) and the
// real controller decision. Used by tools/dev_device_simulator.py.
// usage: plan_timeline_cli <prices.csv> <start> <count> <profile> <target|-1> <min|0> <max|0>
//                          <addon_per_kwh> <currency> <area> <indoor_c> [base_fraction]
#include "../tests/profile_sim.hpp"
#include "cottage/plan_timeline.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc,char** argv) {
    using namespace enevina::cottage;
    if(argc<12) {std::fprintf(stderr,"usage: see source\n");return 2;}
    profile_sim::Series s;
    if(!profile_sim::load_series(argv[1],s)) {std::fprintf(stderr,"cannot read prices\n");return 1;}
    const size_t start=static_cast<size_t>(std::strtoul(argv[2],nullptr,10));
    size_t count=static_cast<size_t>(std::strtoul(argv[3],nullptr,10));
    OptimizationProfile profile{};
    if(!parse_profile(argv[4],profile)) return 2;
    const double target=std::atof(argv[5]),cmin=std::atof(argv[6]),cmax=std::atof(argv[7]);
    const double addon=std::atof(argv[8]),indoor=std::atof(argv[11]);
    const double base_fraction=argc>12?std::atof(argv[12]):0.10;
    if(start>=s.utc.size()) return 2;
    if(start+count>s.utc.size()) count=s.utc.size()-start;
    std::vector<int64_t> utc;std::vector<double> total;
    for(size_t i=0;i<count;++i) {utc.push_back(s.utc[start+i]);total.push_back(s.per_mwh[start+i]/1000.0+addon);}
    TimelineParams params;TimelineMeta meta;
    if(!timeline_setup(profile,target,cmin,cmax,base_fraction,params,meta)) return 2;
    static Timeline timeline;
    if(!build_timeline(utc.data(),total.data(),utc.size(),params,timeline)) {
        std::puts("{\"valid\":false,\"slots\":[],\"segments\":[]}");return 0;
    }
    meta.currency=argv[9];meta.area=argv[10];
    // the real controller, fed exactly like the firmware feeds it
    BootstrapInput in{};
    size_t window=8;
    in.indoor_fresh=true;in.indoor_c=indoor;in.desired_room_c=target>=5?target:21;
    if(!apply_profile(in,profile,target>=5?cmin:0,target>=5?cmax:0,base_fraction,window)) return 2;
    const size_t n=std::min(window,timeline.slot_count);
    in.next_total_prices=total.data();in.price_count=n;in.prices_contiguous_and_fresh=n>=8;
    const BootstrapDecision d=bootstrap_price_decision(in);
    meta.decision_action=bootstrap_action_name(d.action);
    meta.decision_reason=bootstrap_reason_name(d.reason);
    meta.decision_price_valid=d.price_data_valid;
    meta.decision_setpoint_c=d.action==BootstrapAction::HeatSetpoint?d.heat_setpoint_c:-1;
    timeline_json(meta,timeline,[](const char* part){std::fputs(part,stdout);});
    std::fputc('\n',stdout);
    return 0;
}
