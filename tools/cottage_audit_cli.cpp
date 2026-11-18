// Optimization audit runner for the HEAT 16 C / OFF cottage model.
//
// usage: cottage_audit_cli <scenario.txt> {thermostat|price|mpc|mpc_closed_loop}
//
// The scenario file is produced by tools/cottage_optimization_audit.py:
//   params <20 numbers, order below>
//   tariff <margin> <transfer_day> <transfer_night> <tax> <vat> <tax_includes_vat 0|1>
//   initial <indoor_c> <heat_on 0|1> <minutes_in_mode> <target_c>
//   slot <utc> <spot_eur_per_kwh> <forecast_outdoor_c> <actual_outdoor_c> <gain_kw> <defrost>
//
// Total prices are computed HERE, once, with the firmware's own
// finnish_day_transfer() and calculate_total_price(); every policy and the
// Python MILP/replay consume exactly these totals. "mpc" plans once with the
// actual weather (perfect forecast). "mpc_closed_loop" re-plans every slot
// with the FORECAST weather over the remaining horizon and executes on the
// ACTUAL weather, as the firmware does every 15 minutes.
#include "cottage/baseline_policies.hpp"
#include "cottage/finnish_tariff.hpp"
#include "cottage/mpc.hpp"
#include "forecast/pricing.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

using namespace enevina::cottage;
namespace {
long peak_rss_kb() {
#if defined(__unix__) || defined(__APPLE__)
    rusage u{};
    getrusage(RUSAGE_SELF,&u);
    return u.ru_maxrss;
#else
    return -1;
#endif
}
struct Scenario {
    Parameters p{};
    enevina::Tariff day{},night{};
    State initial{};
    double target_c=10;
    std::vector<int64_t> utc;
    std::vector<double> spot,total;
    std::vector<Period> forecast,actual;
};
bool load(const char* path,Scenario& s) {
    std::ifstream in(path);
    if(!in) return false;
    std::string line;
    bool got_params=false,got_tariff=false,got_initial=false;
    while(std::getline(in,line)) {
        std::istringstream f(line);
        std::string tag;
        f>>tag;
        if(tag=="params") {
            auto& p=s.p;
            f>>p.thermal_mass_kwh_per_c>>p.loss_kw_per_c>>p.heat_kw_at_zero_c
             >>p.heat_kw_change_per_outdoor_c>>p.minimum_heat_kw>>p.cop_at_zero_c
             >>p.cop_change_per_outdoor_c>>p.minimum_cop>>p.maximum_cop
             >>p.heat_standby_kw>>p.off_standby_kw>>p.startup_electric_kwh
             >>p.startup_delay_minutes>>p.planning_min_c>>p.critical_c
             >>p.planning_uncertainty_c>>p.maximum_indoor_c>>p.switching_penalty_eur
             >>p.min_heat_minutes>>p.min_off_minutes;
            got_params=!f.fail();
        } else if(tag=="tariff") {
            int incl=0;
            f>>s.day.margin_eur_per_kwh>>s.day.transfer_eur_per_kwh
             >>s.night.transfer_eur_per_kwh>>s.day.electricity_tax_eur_per_kwh
             >>s.day.vat_rate>>incl;
            s.day.electricity_tax_includes_vat=incl!=0;
            const double night_transfer=s.night.transfer_eur_per_kwh;
            s.night=s.day;
            s.night.transfer_eur_per_kwh=night_transfer;
            got_tariff=!f.fail();
        } else if(tag=="initial") {
            int on=0;
            f>>s.initial.indoor_c>>on>>s.initial.minutes_in_mode>>s.target_c;
            s.initial.heat_requested=on!=0;
            got_initial=!f.fail();
        } else if(tag=="slot") {
            long long utc=0;
            double spot=0,fo=0,ao=0,gain=0,defrost=1;
            f>>utc>>spot>>fo>>ao>>gain>>defrost;
            if(f.fail()) return false;
            bool daytime=false;
            if(!finnish_day_transfer(utc,daytime)) return false;
            enevina::Tariff t=daytime?s.day:s.night;
            t.spot_eur_per_kwh=spot;
            double total=0;
            if(!enevina::calculate_total_price(t,total)) return false;
            s.utc.push_back(utc);
            s.spot.push_back(spot);
            s.total.push_back(total);
            s.forecast.push_back({fo,total,gain,defrost});
            s.actual.push_back({ao,total,gain,defrost});
        }
    }
    return got_params && got_tariff && got_initial &&
        !s.actual.empty() && s.actual.size()<=192;
}
const char* mpc_status(MpcStatus x) {
    switch(x) {
    case MpcStatus::Ok:return "OK";
    case MpcStatus::InvalidInput:return "INVALID_INPUT";
    case MpcStatus::UnsafeStart:return "UNSAFE_START";
    case MpcStatus::Infeasible:return "INFEASIBLE";
    case MpcStatus::ResourceLimit:return "RESOURCE_LIMIT";
    case MpcStatus::ReplayFailed:return "REPLAY_FAILED";
    case MpcStatus::Timeout:return "TIMEOUT";
    case MpcStatus::Cancelled:return "CANCELLED";
    }
    return "UNKNOWN";
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=3) {
        std::fprintf(stderr,"usage: cottage_audit_cli scenario.txt "
            "{thermostat|price|mpc|mpc_closed_loop}\n");
        return 2;
    }
    Scenario s;
    if(!load(argv[1],s)) {std::fprintf(stderr,"bad scenario\n");return 2;}
    const std::string policy=argv[2];
    const size_t n=s.actual.size();
    std::vector<uint8_t> schedule(n,0);
    std::string status="OK";
    double solve_ms_max=0,solve_ms_sum=0;
    unsigned solves=0;
    using clock=std::chrono::steady_clock;
    if(policy=="thermostat"||policy=="price") {
        BaselineOptions o{};
        o.target_c=s.target_c;
        const auto begin=clock::now();
        const auto r=simulate_baseline(policy=="price"?BaselinePolicy::PriceThreshold:
            BaselinePolicy::Thermostat,s.p,s.initial,s.actual.data(),n,o);
        solve_ms_max=solve_ms_sum=std::chrono::duration<double,std::milli>(
            clock::now()-begin).count();
        solves=1;
        if(!r.ok) status="INVALID_INPUT";
        for(size_t i=0;i<n;++i) schedule[i]=r.heat_16_c[i];
    } else if(policy=="mpc") {
        const auto begin=clock::now();
        MpcRunControls limits{};
        limits.runtime_budget_ms=60000; // host measurement; ESP32 uses 1800 ms
        const auto r=optimize(s.p,s.initial,s.actual.data(),n,3*1024*1024,limits);
        solve_ms_max=solve_ms_sum=std::chrono::duration<double,std::milli>(
            clock::now()-begin).count();
        solves=1;
        status=mpc_status(r.status);
        for(size_t i=0;i<n;++i) schedule[i]=r.heat_16_c[i];
    } else if(policy=="mpc_closed_loop") {
        State state=s.initial;
        for(size_t t=0;t<n;++t) {
            const auto begin=clock::now();
            MpcRunControls limits{};
            limits.runtime_budget_ms=60000;
            const auto r=optimize(s.p,state,s.forecast.data()+t,n-t,3*1024*1024,limits);
            const double ms=std::chrono::duration<double,std::milli>(
                clock::now()-begin).count();
            solve_ms_max=std::max(solve_ms_max,ms);
            solve_ms_sum+=ms;
            ++solves;
            bool on;
            if(r.status==MpcStatus::Ok) on=r.heat_16_c[0]!=0;
            else {
                // Same fail-safe direction as the firmware supervisor: no
                // plan -> pump's own HEAT 16 C thermostat (dwell permitting).
                if(status=="OK") status=std::string("FALLBACK_")+mpc_status(r.status);
                on=true;
                const unsigned need=state.heat_requested?s.p.min_heat_minutes:
                                                         s.p.min_off_minutes;
                if(on!=state.heat_requested && state.minutes_in_mode<need)
                    on=state.heat_requested;
                if(state.indoor_c>=s.p.maximum_indoor_c-0.05) on=false;
                if(on!=state.heat_requested && state.minutes_in_mode<need)
                    on=state.heat_requested;
            }
            schedule[t]=on?1:0;
            state=advance(s.p,state,s.actual[t],on).next;
        }
    } else {
        std::fprintf(stderr,"unknown policy\n");
        return 2;
    }
    std::printf("{\"policy\":\"%s\",\"status\":\"%s\",\"solves\":%u,"
        "\"solve_ms_max\":%.4f,\"solve_ms_mean\":%.4f,\"peak_rss_kb\":%ld,"
        "\"total_price\":[",policy.c_str(),status.c_str(),solves,
        solve_ms_max,solves?solve_ms_sum/solves:0.0,peak_rss_kb());
    for(size_t i=0;i<n;++i) std::printf("%s%.10f",i?",":"",s.total[i]);
    std::printf("],\"schedule\":[");
    for(size_t i=0;i<n;++i) std::printf("%s%u",i?",":"",static_cast<unsigned>(schedule[i]));
    std::puts("]}");
    return 0;
}
