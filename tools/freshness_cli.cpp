// Prints /api/freshness JSON for a scenario using the REAL assess_freshness and
// JSON writer (cottage/data_freshness.hpp). Used by tools/dev_device_simulator.py.
// usage: freshness_cli <hours_since_last_fetch> <wifi 0|1> <sensor_fresh 0|1> <price_error>
//                      <zone_configured 0|1> <clock 0|1> <armed 0|1>
#include "cottage/data_freshness.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) {
    using namespace enevina::cottage;
    if(argc!=8) {std::fprintf(stderr,"usage: see source\n");return 2;}
    const double hours=std::atof(argv[1]);
    constexpr int64_t T0=1795000000;                    // the moment of the last good fetch
    FreshnessInputs in;
    in.now=T0+static_cast<int64_t>(hours*3600);
    in.clock_trusted=std::atoi(argv[6])!=0;in.wifi_ok=std::atoi(argv[2])!=0;
    in.price_zone_configured=std::atoi(argv[5])!=0;
    in.price_last_fetch=T0;in.price_valid_until=T0+20*3600;     // day-ahead data published at 13:00 covers ~11-35 h
    in.price_current_valid=in.now<in.price_valid_until;
    in.price_error=static_cast<uint8_t>(std::atoi(argv[4]));
    in.weather_configured=true;in.weather_fetched=T0-1800;in.weather_valid_until=T0+30*3600;
    in.weather_available=in.now<in.weather_valid_until;
    in.sensor_bound=true;in.sensor_fresh=std::atoi(argv[3])!=0;in.sensor_age_s=in.sensor_fresh?20:1500;
    in.pump_configured=true;in.pump_fresh=true;in.pump_age_s=5;
    in.control_armed=std::atoi(argv[7])!=0;
    const Freshness f=assess_freshness(in);
    freshness_json(f,in.control_armed,[](const char* p){std::fputs(p,stdout);});
    std::fputc('\n',stdout);
    return 0;
}
