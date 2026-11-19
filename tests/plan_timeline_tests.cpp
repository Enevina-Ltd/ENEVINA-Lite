// Plan timeline on real recorded prices (7 days FI/DK1/SE3): the timeline must
// agree slot by slot with the controller that actually decides heating.
#include "cottage/plan_timeline.hpp"
#include "forecast/met_norway.hpp"      // strict JSON validator for the output
#include "tests/profile_sim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef ENEVINA_TEST_DATA_DIR
#error "ENEVINA_TEST_DATA_DIR must point at tests/data"
#endif
using namespace enevina::cottage;
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)

static bool load(const char* name,std::vector<int64_t>& utc,std::vector<double>& total,double addon) {
    profile_sim::Series s;
    if(!profile_sim::load_series(std::string(ENEVINA_TEST_DATA_DIR)+"/prices/"+name,s)) return false;
    for(size_t i=0;i<s.utc.size();++i) {utc.push_back(s.utc[i]);total.push_back(s.per_mwh[i]/1000.0+addon);}
    return true;
}

// The controller's own decision, with the room just inside the thermostat band
// so that only the price class can decide between heat/off.
static BootstrapReason controller_reason(const std::vector<double>& total,size_t i,size_t window,
                                         double fraction,double indoor,double target) {
    BootstrapInput in{};
    in.indoor_fresh=true;in.indoor_c=indoor;in.desired_room_c=target;
    const size_t n=std::min(window,total.size()-i);
    in.next_total_prices=total.data()+i;in.price_count=n;in.prices_contiguous_and_fresh=true;
    in.min_price_advantage_fraction=fraction;
    return bootstrap_price_decision(in).reason;
}

static void agrees_with_the_controller_on_every_slot() {
    struct Z {const char* file;double addon;};
    const Z zones[]={{"FI_EUR_2026-11-12_to_18.csv",0.06},{"DK1_EUR_2026-11-12_to_18.csv",0.06},
                     {"SE3_SEK_2026-11-12_to_18.csv",0.70}};
    size_t cheap=0,expensive=0,neutral=0,unknown=0;
    for(const auto& z:zones) {
        std::vector<int64_t> utc;std::vector<double> total;
        CHECK(load(z.file,utc,total,z.addon));
        for(auto prof:{OptimizationProfile::Comfort,OptimizationProfile::Balanced,OptimizationProfile::Saving}) {
            TimelineParams params;TimelineMeta meta;
            CHECK(timeline_setup(prof,21,19,23,0.10,params,meta));
            // several starting points, each a fresh 48 h view like the device would build
            for(size_t start=0;start+192<=total.size();start+=37) {
                Timeline t;
                CHECK(build_timeline(utc.data()+start,total.data()+start,192,params,t));
                CHECK(t.slot_count==192&&!t.truncated_at_gap);
                for(size_t i=0;i<t.slot_count;++i) {
                    const size_t n=std::min(params.window_slots,t.slot_count-i);
                    if(n<8) {CHECK(t.slots[i].cls==PriceClass::Unknown);++unknown;continue;}
                    // Comfort-style probe: below target -> cheap pre-heats, above target -> expensive coasts.
                    const auto below=controller_reason(total,start+i,n,params.advantage_fraction,20.6,21);
                    const auto above=controller_reason(total,start+i,n,params.advantage_fraction,21.4,21);
                    // indoor 20.6 is inside the thermostat band: CheapWindow only when the slot is cheap
                    CHECK((t.slots[i].cls==PriceClass::Cheap)==(below==BootstrapReason::CheapWindow));
                    // indoor 21.4 is >= target and inside the band: ExpensiveWindow only when expensive
                    CHECK((t.slots[i].cls==PriceClass::Expensive)==(above==BootstrapReason::ExpensiveWindow));
                    switch(t.slots[i].cls) {
                    case PriceClass::Cheap:++cheap;break;
                    case PriceClass::Expensive:++expensive;break;
                    case PriceClass::Neutral:++neutral;break;
                    default:++unknown;
                    }
                }
            }
        }
    }
    // The comparison is not vacuous: all three classes occur in real data.
    CHECK(cheap>500&&expensive>500&&neutral>500&&unknown>0);
}

static void edges_and_gaps() {
    std::vector<int64_t> utc;std::vector<double> total;
    CHECK(load("FI_EUR_2026-11-12_to_18.csv",utc,total,0.06));
    TimelineParams params;TimelineMeta meta;
    CHECK(timeline_setup(OptimizationProfile::Balanced,21,19,23,0.10,params,meta));
    Timeline t;
    // fewer than 8 slots: nothing can be classified, but the slots are listed as unknown
    CHECK(build_timeline(utc.data(),total.data(),7,params,t));
    for(size_t i=0;i<7;++i) CHECK(t.slots[i].cls==PriceClass::Unknown);
    CHECK(t.segment_count==0);
    // a missing slot truncates, never bridges
    std::vector<int64_t> gap(utc.begin(),utc.begin()+100);
    gap[60]+=900;                                             // slot 60 starts 15 min late
    for(size_t i=61;i<gap.size();++i) gap[i]=utc[i]+900;
    CHECK(build_timeline(gap.data(),total.data(),100,params,t));
    CHECK(t.truncated_at_gap&&t.slot_count==60);
    // misaligned start, empty input, bad values
    std::vector<int64_t> odd(utc.begin(),utc.begin()+20);for(auto& u:odd) u+=60;
    CHECK(!build_timeline(odd.data(),total.data(),20,params,t));
    CHECK(!build_timeline(utc.data(),total.data(),0,params,t));
    CHECK(!build_timeline(nullptr,total.data(),10,params,t));
    std::vector<double> bad(total.begin(),total.begin()+50);
    bad[30]=std::nan("");
    CHECK(build_timeline(utc.data(),bad.data(),50,params,t)&&t.slot_count==30&&t.truncated_at_gap);
    bad[30]=99.0;
    CHECK(build_timeline(utc.data(),bad.data(),50,params,t)&&t.slot_count==30);
    TimelineParams tiny=params;tiny.window_slots=4;
    CHECK(!build_timeline(utc.data(),total.data(),50,tiny,t));
    // capacity: never more than 192 slots
    CHECK(build_timeline(utc.data(),total.data(),400,params,t)&&t.slot_count==192);
}

static void segments_are_consistent() {
    std::vector<int64_t> utc;std::vector<double> total;
    CHECK(load("DK1_EUR_2026-11-12_to_18.csv",utc,total,0.06));
    TimelineParams params;TimelineMeta meta;
    CHECK(timeline_setup(OptimizationProfile::Saving,21,19,23,0.10,params,meta));
    Timeline t;
    CHECK(build_timeline(utc.data()+100,total.data()+100,192,params,t));
    CHECK(t.segment_count>=2);
    int64_t last_end=0;
    bool saw_cheap=false,saw_expensive=false;
    for(size_t i=0;i<t.segment_count;++i) {
        const auto& g=t.segments[i];
        CHECK(g.start>=last_end&&g.end>g.start&&(g.end-g.start)==static_cast<int64_t>(g.slots)*900);
        CHECK(g.slots>=params.min_segment_slots);
        CHECK(g.min_total<=g.avg_total+1e-12&&g.avg_total<=g.max_total+1e-12);
        // every slot inside carries the segment's class
        for(size_t k=0;k<t.slot_count;++k)
            if(t.slots[k].utc>=g.start&&t.slots[k].utc<g.end) CHECK(t.slots[k].cls==g.cls);
        // cheap periods average below their window median, expensive above
        if(g.cls==PriceClass::Cheap) {CHECK(g.avg_total<g.avg_median);saw_cheap=true;}
        if(g.cls==PriceClass::Expensive) {CHECK(g.avg_total>g.avg_median);saw_expensive=true;}
        last_end=g.end;
    }
    CHECK(saw_cheap&&saw_expensive);
}

static std::string json_of(const TimelineMeta& m,const Timeline& t) {
    std::string out;
    timeline_json(m,t,[&](const char* part){out+=part;});
    return out;
}

static void json_is_valid_and_honest() {
    std::vector<int64_t> utc;std::vector<double> total;
    CHECK(load("SE3_SEK_2026-11-12_to_18.csv",utc,total,0.70));
    TimelineParams params;TimelineMeta meta;
    CHECK(timeline_setup(OptimizationProfile::Saving,21,19,23,0.10,params,meta));
    CHECK(std::fabs(meta.preheat_c-0.8*(22.25-21))<1e-9&&std::fabs(meta.coast_c-0.8*(21-19.75))<1e-9);   // same call as the controller
    meta.currency="SEK";meta.area="SE3";meta.decision_action="HEAT_SETPOINT";meta.decision_reason="LOW_COST_WINDOW";
    meta.decision_setpoint_c=22.5;meta.decision_price_valid=true;
    Timeline t;
    CHECK(build_timeline(utc.data()+40,total.data()+40,192,params,t));
    const std::string j=json_of(meta,t);
    enevina::met_detail::Json v{j.data(),j.data()+j.size()};
    CHECK(v.value());v.ws();CHECK(v.cur==v.end);                       // strictly valid JSON
    CHECK(j.find("nan")==std::string::npos&&j.find("inf")==std::string::npos);
    CHECK(j.find("\"valid\":true")!=std::string::npos&&j.find("\"profile\":\"saving\"")!=std::string::npos);
    CHECK(j.find("\"currency\":\"SEK\"")!=std::string::npos&&j.find("\"setpoint_c\":22.5")!=std::string::npos);
    // there is NO predicted indoor temperature or cost in the document
    for(const char* forbidden:{"indoor","predicted","cost","savings"}) CHECK(j.find(forbidden)==std::string::npos);
    // 192 slots: output stays small enough to stream
    CHECK(j.size()<30000);
    // no target (mode without one): headroom null, classes still present
    TimelineParams p2;TimelineMeta m2;
    CHECK(timeline_setup(OptimizationProfile::Saving,-1,19,23,0.10,p2,m2));
    CHECK(m2.preheat_c==0&&m2.coast_c==0&&m2.target_c<0);
    Timeline t2;
    CHECK(build_timeline(utc.data(),total.data(),96,p2,t2));
    const std::string j2=json_of(m2,t2);
    CHECK(j2.find("\"target_c\":null")!=std::string::npos&&j2.find("\"comfort_min_c\":null")!=std::string::npos);
    enevina::met_detail::Json v2{j2.data(),j2.data()+j2.size()};
    CHECK(v2.value());
    // empty timeline serializes as valid:false with empty arrays
    Timeline none;
    const std::string j3=json_of(m2,none);
    CHECK(j3.find("\"valid\":false")!=std::string::npos&&j3.find("\"slots\":[]")!=std::string::npos);
    // profile changes the threshold: Comfort (2x) finds fewer cheap periods than Saving (0.5x)
    TimelineParams pc;TimelineMeta mc;
    CHECK(timeline_setup(OptimizationProfile::Comfort,21,19,23,0.10,pc,mc));
    Timeline tc,ts;
    CHECK(build_timeline(utc.data(),total.data(),192,pc,tc));
    CHECK(build_timeline(utc.data(),total.data(),192,params,ts));
    size_t cc=0,cs=0;
    for(size_t i=0;i<192;++i) {cc+=tc.slots[i].cls==PriceClass::Cheap;cs+=ts.slots[i].cls==PriceClass::Cheap;}
    CHECK(cs>cc);
}

int main() {
    agrees_with_the_controller_on_every_slot();
    edges_and_gaps();
    segments_are_consistent();
    json_is_valid_and_honest();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("plan_timeline_tests OK");
    return 0;
}
