// Host resource measurement for the live MPC chain (NOT an ESP32 result).
// Measures, on this PC: plan_mpc() runtime for 96/192-slot horizons over
// several weather/price cases, the TRUE peak heap of one plan (global
// operator new is counted), identifier update / parameter build time, and
// the static sizes the firmware must place (stack vs PSRAM).
// usage: cottage_mpc_resource_bench [json_out]
#include "tests/cottage_closed_loop.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace {
std::atomic<size_t> live{0},peak{0};
struct Header {size_t n;size_t pad;};
}
void* operator new(size_t n) {
    auto* h=static_cast<Header*>(std::malloc(n+sizeof(Header)));
    if(!h) throw std::bad_alloc();
    h->n=n;
    const size_t now=live.fetch_add(n)+n;
    size_t p=peak.load();
    while(now>p && !peak.compare_exchange_weak(p,now)) {}
    return h+1;
}
void operator delete(void* ptr) noexcept {
    if(!ptr) return;
    auto* h=static_cast<Header*>(ptr)-1;
    live.fetch_sub(h->n);
    std::free(h);
}
void operator delete(void* ptr,size_t) noexcept {operator delete(ptr);}

using namespace closed_loop;
using clk=std::chrono::steady_clock;

int main(int argc,char** argv) {
    // A calibrated identifier from the simulated learning history.
    Scenario sc=scenarios()[5];sc.eval_slots=4;
    Harness h(sc);
    BuildingIdentifier id;
    h.run(Kind::Mpc,&id);
    const int64_t now=sc.t0+3600;
    std::string json="{\"evidence\":\"HOST_PC_ONLY_NOT_ESP32\",";
    json+="\"calibration\":\""+std::string(calibration_state_name(id.report(now).state))+"\",";
    char buf[512];
    std::snprintf(buf,sizeof(buf),"\"sizes_bytes\":{\"BuildingIdentifier\":%zu,"
        "\"HorizonResult\":%zu,\"PlannerOutput\":%zu,\"PlanRow_x192\":%zu,"
        "\"MpcResult\":%zu,\"identifier_blob\":%zu},",sizeof(BuildingIdentifier),
        sizeof(HorizonResult),sizeof(PlannerOutput),sizeof(PlanRow)*max_horizon_slots,
        sizeof(MpcResult),BuildingIdentifier::blob_capacity);
    json+=buf;
    json+="\"plans\":[";
    struct Case {const char* name;double base;double swing;bool spike;bool flat;};
    const Case cases[]={{"mild_daily_prices",-3,3,false,false},{"cold_-10C",-10,1,false,false},
                        {"price_spike",-4,3,true,false},{"flat_price",-4,3,false,true}};
    bool first=true;
    double worst_ms=0;size_t worst_heap=0;
    for(const size_t slots:{size_t(96),size_t(192)}) for(const Case& c:cases) {
        std::vector<Spot15> prices;
        std::vector<enevina::OutdoorPoint> weather;
        for(size_t i=0;i<200;++i) {
            double spot=c.flat?0.10:base_spot(i);
            if(c.spike && i>=60 && i<72) spot=1.2;
            prices.push_back({now+static_cast<int64_t>(i)*900,spot});
        }
        for(size_t k=0;k<64;++k)
            weather.push_back({now+static_cast<int64_t>(k)*3600,
                c.base+c.swing*std::sin(6.283185307179586*k/24.0)});
        double ms_max=0,ms_sum=0;size_t heap=0;int ok=0;const int reps=20;
        for(int r=0;r<reps;++r) {
            HorizonResult hz=join_forecast_horizon(now,now-600,now-600,prices.data(),
                prices.size(),weather.data(),weather.size(),sim_pricer,nullptr,slots,32);
            PlannerInput pi{};
            pi.now_utc=now;pi.identifier=&id;pi.heating_allowed=true;pi.target_valid=true;
            pi.target_c=12.0;pi.temperature_fresh=true;pi.indoor_c=12.0;
            pi.pump_state_known=true;pi.pump_heat=false;pi.minutes_in_mode=120;
            pi.horizon=&hz;pi.controls.runtime_budget_ms=60000;
            std::vector<PlanRow> rows(max_horizon_slots);
            const size_t base=live.load();
            peak.store(base);
            const auto t0=clk::now();
            const PlannerOutput o=plan_mpc(pi,rows.data());
            const double ms=std::chrono::duration<double,std::milli>(clk::now()-t0).count();
            heap=std::max(heap,peak.load()-base);
            ms_max=std::max(ms_max,ms);ms_sum+=ms;
            ok+=o.record.has_plan?1:0;
        }
        worst_ms=std::max(worst_ms,ms_max);worst_heap=std::max(worst_heap,heap);
        std::snprintf(buf,sizeof(buf),"%s{\"case\":\"%s\",\"slots\":%zu,\"ok\":%d,\"reps\":%d,"
            "\"ms_max\":%.3f,\"ms_mean\":%.3f,\"peak_heap_bytes\":%zu}",first?"":",",c.name,
            slots,ok,reps,ms_max,ms_sum/reps,heap);
        json+=buf;first=false;
        std::printf("%-18s %3zu slots ok %2d/%d  max %.3f ms  mean %.3f ms  peak heap %zu B\n",
                    c.name,slots,ok,reps,ms_max,ms_sum/reps,heap);
    }
    json+="],";
    // Identifier per-slot cost.
    BuildingIdentifier probe;
    Scenario s2=scenarios()[5];
    Harness h2(s2);
    const auto t0=clk::now();
    BuildingIdentifier tmp;
    h2.run(Kind::Mpc,&tmp);   // includes plant simulation + MPC; upper bound only
    const double run_ms=std::chrono::duration<double,std::milli>(clk::now()-t0).count();
    SlotObservation o{};
    o.slot_utc=now;o.indoor_valid=true;o.indoor_start_c=12;o.indoor_end_c=11.9;
    o.outdoor_valid=true;o.outdoor_c=-3;o.heat_valid=true;
    const auto a0=clk::now();
    for(int i=0;i<10000;++i) {o.slot_utc+=900;o.heat_requested=(i/4)%2;probe.add(o);}
    const double add_us=std::chrono::duration<double,std::micro>(clk::now()-a0).count()/10000;
    Parameters p{};bool kwh=false;ComfortBand band{};mpc_comfort_band(12.0,band);
    const auto b0=clk::now();
    for(int i=0;i<1000;++i) id.build_parameters(now,band,false,1.0,p,kwh);
    const double build_us=std::chrono::duration<double,std::micro>(clk::now()-b0).count()/1000;
    std::vector<uint8_t> blob(BuildingIdentifier::blob_capacity);size_t used=0;
    id.save(1,blob.data(),blob.size(),used);
    std::snprintf(buf,sizeof(buf),"\"identifier_add_us\":%.3f,\"build_parameters_us\":%.3f,"
        "\"identifier_blob_used_bytes\":%zu,\"worst_plan_ms\":%.3f,\"worst_plan_heap_bytes\":%zu,"
        "\"full_closed_loop_scenario_ms\":%.1f}\n",add_us,build_us,used,worst_ms,worst_heap,run_ms);
    json+=buf;
    std::printf("identifier add %.3f us/slot, build_parameters %.3f us, blob %zu B, worst plan %.3f ms, "
                "worst plan heap %zu B\n",add_us,build_us,used,worst_ms,worst_heap);
    if(argc>1) {
        std::FILE* f=std::fopen(argv[1],"wb");
        if(!f) return 1;
        std::fwrite(json.data(),1,json.size(),f);std::fclose(f);
    }
    return 0;
}
