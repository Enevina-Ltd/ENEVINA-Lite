// Closed-loop comparison: thermostat vs Lite price control vs calibrated MPC
// on identical simulated buildings, weather, prices and initial states.
//
// usage: cottage_closed_loop_cli [json_out] [markdown_out]
//
// SIMULATION ONLY. Results are not field savings. See
// docs/MPC_CONTROL_20261120.md for method and interpretation.
// C stdio only (no iostreams): MinGW hosts may load a mismatched
// libstdc++ DLL whose stream ABI crashes.
#include "tests/cottage_closed_loop.hpp"
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>

using namespace closed_loop;

namespace {
std::string fmt(const char* f,...) __attribute__((format(printf,1,2)));
std::string fmt(const char* f,...) {
    char buf[2048];
    va_list ap;
    va_start(ap,f);
    std::vsnprintf(buf,sizeof(buf),f,ap);
    va_end(ap);
    return buf;
}
std::string json_map(const std::map<std::string,unsigned>& m) {
    std::string o="{";
    bool first=true;
    for(const auto& [k,v]:m) {o+=fmt("%s\"%s\":%u",first?"":",",k.c_str(),v);first=false;}
    return o+"}";
}
std::string json_metrics(const Metrics& m) {
    std::string o=fmt("{\"controller\":\"%s\",\"cost_eur\":%.4f,\"equalized_cost_eur\":%.4f,"
        "\"electric_kwh\":%.4f,\"thermal_kwh\":%.4f,\"final_c\":%.4f,\"min_c\":%.4f,"
        "\"max_c\":%.4f,\"below_band_kh\":%.4f,\"above_band_kh\":%.4f,"
        "\"violation_slots\":%u,\"compressor_starts\":%u",m.controller.c_str(),m.cost,
        m.equalized_cost,m.electric_kwh,m.thermal_kwh,m.final_c,m.min_c,m.max_c,
        m.below_kh,m.above_kh,m.violation_slots,m.starts);
    if(m.controller=="calibrated_mpc") {
        o+=fmt(",\"mpc_active_slots\":%u,\"mpc_solves\":%u,\"solver_failures\":%u,"
               "\"solve_ms_max\":%.4f,\"solve_ms_mean\":%.4f,\"calibration_at_end\":\"%s\","
               "\"calibrated_during_eval_after_slots\":",m.mpc_active,m.mpc_attempts,
               m.solver_failures,m.solve_ms_max,
               m.mpc_attempts?m.solve_ms_sum/m.mpc_attempts:0.0,m.calibration_state.c_str());
        o+=m.calibrated_at_slot==static_cast<unsigned>(-1)?std::string("null"):
            fmt("%u",m.calibrated_at_slot);
        o+=",\"fallback_slots\":"+json_map(m.fallbacks);
        o+=",\"solver_failure_status\":"+json_map(m.solver_status);
    }
    return o+"}";
}
} // namespace

int main(int argc,char** argv) {
    const auto all=scenarios();
    std::string json="{\"evidence\":\"SIMULATED_HOST_ONLY\",\"note\":\"Synthetic building "
        "and prices; not a field result. Equalized cost adds the electricity needed to "
        "return to the common initial temperature (true COP at the final outdoor "
        "temperature, mean evaluation price).\",\"scenarios\":[";
    std::string md="| Scenario | Controller | Cost € | Equal-energy € | kWh | Final °C | "
        "Min °C | Below band K·h | Starts | MPC active / fallback |\n"
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---|\n";
    bool first=true;
    for(const auto& s:all) {
        Harness h(s);
        const Metrics t=h.run(Kind::Thermostat);
        const Metrics p=h.run(Kind::Price);
        const Metrics m=h.run(Kind::Mpc);
        json+=fmt("%s{\"name\":\"%s\",\"description\":\"%s\",\"target_c\":%.2f,"
            "\"initial_c\":%.2f,\"learn_slots\":%zu,\"eval_slots\":%zu,\"pump_only_meter\":%s,"
            "\"results\":[",first?"":",",s.name.c_str(),s.description.c_str(),s.target_c,
            s.initial_c,s.learn_slots,s.eval_slots,s.meter_pump_only?"true":"false");
        json+=json_metrics(t)+","+json_metrics(p)+","+json_metrics(m)+"]}";
        first=false;
        for(const Metrics* r:{&t,&p,&m}) {
            std::string extra="—";
            if(r==&m) {
                extra=fmt("%u/%zu",m.mpc_active,s.eval_slots);
                unsigned best=0;
                std::string why;
                for(const auto& [k,v]:m.fallbacks) if(v>best) {best=v;why=k;}
                if(best) extra+=fmt(" (most: %s %u)",why.c_str(),best);
                if(m.solver_failures) extra+=fmt(", solver failures %u %s",m.solver_failures,
                                                 json_map(m.solver_status).c_str());
            }
            md+=fmt("| %s | %s | %.3f | %.3f | %.2f | %.2f | %.2f | %.2f | %u | %s |\n",
                r==&t?s.name.c_str():"",r->controller.c_str(),r->cost,r->equalized_cost,
                r->electric_kwh,r->final_c,r->min_c,r->below_kh,r->starts,extra.c_str());
        }
    }
    json+="]}\n";
    std::fputs(md.c_str(),stdout);
    for(int a=1;a<argc && a<=2;++a) {
        std::FILE* f=std::fopen(argv[a],"wb");
        if(!f) {std::fprintf(stderr,"cannot write %s\n",argv[a]);return 1;}
        const std::string& body=a==1?json:md;
        std::fwrite(body.data(),1,body.size(),f);
        std::fclose(f);
    }
    return 0;
}
