#include "optimizer/optimizer.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

using namespace enevina;

static std::vector<double> numbers(const std::string& line) {
    std::vector<double> values;
    std::stringstream stream(line);
    std::string item;
    while (std::getline(stream, item, ',')) values.push_back(std::stod(item));
    return values;
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: enevina_cli scenario.csv\n"; return 2; }
    std::ifstream input(argv[1]);
    std::string line;
    if (!input || !std::getline(input, line)) return 2;
    try {
        const auto x = numbers(line);
        if (x.size() != 20) return 2;
        Parameters p{};
        p.building_capacity_kwh_per_c=x[0]; p.building_loss_kw_per_c=x[1];
        p.tank_capacity_kwh=x[2]; p.tank_loss_kw_per_kwh=x[3];
        p.pump_max_thermal_kw=x[4]; p.pump_derate_kw_per_c_below_zero=x[5];
        p.cop_at_reference=x[6]; p.cop_outdoor_per_c=x[7];
        p.cop_supply_per_c=x[8]; p.minimum_cop=x[9]; p.maximum_cop=x[10];
        p.emitter_kw_per_c=x[11]; p.space_supply_low_c=x[12];
        p.space_supply_high_c=x[13]; p.dhw_supply_c=x[14];
        p.indoor_min_c=x[15]; p.indoor_max_c=x[16]; p.tank_min_kwh=x[17];
        const State initial{x[18],x[19]};
        std::vector<Period> periods;
        while (std::getline(input,line)) {
            if (line.empty()) continue;
            const auto d=numbers(line);
            if (d.size()!=4) return 2;
            periods.push_back({d[0],d[1],d[2],d[3],0});
        }
        const auto start=std::chrono::steady_clock::now();
        const auto result=optimize(p,initial,periods.data(),periods.size());
        const double seconds=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-start).count();
        if (result.status!=OptimizeStatus::Ok) {
            std::cerr << "optimization status " << static_cast<int>(result.status) << '\n';
            return 3;
        }
        std::cout << std::setprecision(12) << "cost_eur," << result.cost_eur << '\n'
                  << "seconds," << seconds << '\n';
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS memory{};
        if (GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)))
            std::cout << "peak_rss_bytes," << memory.PeakWorkingSetSize << '\n';
#else
        struct rusage memory{};
        if (getrusage(RUSAGE_SELF,&memory)==0)
            std::cout << "peak_rss_bytes," << memory.ru_maxrss * 1024LL << '\n';
#endif
        for (size_t i=0;i<result.count;++i) {
            const Action a=result.actions[i];
            std::cout << static_cast<int>(a.mode) << ',' << a.requested_thermal_kw
                      << ',' << a.supply_c << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "invalid scenario: " << e.what() << '\n';
        return 2;
    }
}
