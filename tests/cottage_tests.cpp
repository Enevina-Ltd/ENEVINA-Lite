#include "cottage/physics.hpp"
#include "cottage/mpc.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
#include <chrono>
#include "cottage/telemetry.hpp"
using namespace enevina::cottage;
static void check(bool b,const char* msg) {if(!b){std::fprintf(stderr,"FAIL: %s\n",msg);std::exit(1);}}
static Parameters param() {
    Parameters p;
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
    p.planning_min_c=8;
    p.critical_c=5;
    p.maximum_indoor_c=15.8;
    p.switching_penalty_eur=0.03;
    p.min_heat_minutes=60;
    p.min_off_minutes=60;
    return p;
}
static void check_schedule(const Parameters& p,State start,
    const std::vector<Period>& f,const MpcResult& x) {
    check(x.status==MpcStatus::Ok,"optimizer must return feasible result");
    check(x.count==f.size(),"count");
    const double start_t=start.indoor_c;
    double cost=0, energy=0, minimum=start_t;
    unsigned starts=0, switches=0;
    for(size_t i=0;i<f.size();i++) {
        bool on=x.heat_16_c[i]!=0;
        if(on!=start.heat_requested) {
            check(start.minutes_in_mode>=
                (start.heat_requested?p.min_heat_minutes:p.min_off_minutes),
                "minimum dwell must hold");
            switches++;
            if(on)starts++;
        }
        const auto step=advance(p,start,f[i],on);
        minimum=std::min(minimum,step.next.indoor_c);
        check(step.next.indoor_c>=p.planning_min_c+p.planning_uncertainty_c-1e-8,
            "planning minimum");
        check(step.next.indoor_c<=p.maximum_indoor_c+1e-8,"max temperature");
        cost+=step.cost_eur;energy+=step.electric_kwh;start=step.next;
    }
    check(start.indoor_c>=start_t-1e-8,"terminal temperature equality");
    check(std::abs(x.cost_eur-cost)<1e-7,"exact cost replay");
    check(std::abs(x.electric_kwh-energy)<1e-7,"energy replay");
    check(std::abs(x.minimum_c-minimum)<1e-7,"minimum replay");
    check(x.starts==starts,"start counter");
    check(switches < f.size()/3,"avoid rapid command cycling");
}
static void room_temperature_uses_the_selected_fresh_source() {
    using namespace enevina;
    using namespace enevina::cottage;
    constexpr int64_t now=1800000000;
    const TemperatureSample ruuvi{true,now-4,20.6};
    PumpRoomState pump{};
    pump.source=PumpStateSource::Gateway;
    pump.evidence.received_utc=now-5;
    pump.indoor_c=22.5;
    const auto a=room_temperature_sample({},ruuvi,pump,now);
    check(valid_temperature(a,now) && a.indoor_c==20.6,"default uses Ruuvi");
    const RoomTemperatureConfig selected{RoomTemperatureSource::Pump,-150};
    check(!room_temperature_sample(selected,ruuvi,pump,now).received,"unproven pump reading is unavailable");
    pump.source=PumpStateSource::DirectLan;
    const auto b=room_temperature_sample(selected,ruuvi,pump,now);
    check(valid_temperature(b,now) && b.indoor_c==21.0 && b.captured_utc==now-5,"calibrated LAN reading");
    check(!room_temperature_sample(selected,ruuvi,pump,now+90).received,"stale LAN reading");
    pump.source=PumpStateSource::VerifiedBus;
    pump.evidence={now-5,now-200,true};
    check(!room_temperature_sample(selected,ruuvi,pump,now).received,"unproven pump reading is unavailable");
    pump.evidence.source_utc=now-8;
    const auto c=room_temperature_sample(selected,ruuvi,pump,now);
    check(valid_temperature(c,now) && c.captured_utc==now-8,"original verified UART time");
}

static void optimize_residential_setpoint_schedule() {
    auto p=param();
    p.variable_setpoints=true;
    p.thermostat_band_c=1.0;
    p.planning_min_c=20;
    p.critical_c=18;
    p.maximum_indoor_c=23;
    const State start{21,true,120};
    const Period period{8,0.20,0,1};
    const auto reduced=advance_setpoint(p,start,period,21.5);
    const auto full=advance_setpoint(p,start,period,22.0);
    check(reduced.delivered_thermal_kw>0 && reduced.delivered_thermal_kw<full.delivered_thermal_kw,
          "the thermostat reduces output as the room approaches the requested temperature");
    check(advance_setpoint(p,start,period,21).delivered_thermal_kw==0,
          "pump can stay in heat mode with thermostat off");

    std::vector<Period> horizon(96,period);
    for(size_t i=0;i<horizon.size();++i) {
        horizon[i].total_price_eur_per_kwh=(i>=26 && i<62)?0.48:0.08;
    }
    MpcRunControls control;
    control.heat_setpoints_c={20.5,21,21.5,22};
    control.heat_setpoint_count=4;
    control.initial_setpoint_c=21;
    control.setpoint_change_penalty=0.02;
    const auto plan=optimize(p,start,horizon.data(),horizon.size(),3*1024*1024,control);
    check(plan.status==MpcStatus::Ok && plan.count==horizon.size(),"feasible residential schedule");
    State room=start;
    double cost=0;
    double first_heat=0;
    bool varied=false;
    for(size_t i=0;i<plan.count;++i) {
        const double selected=plan.setpoint_c100[i]/100.0;
        check(selected==0 || selected==20.5 || selected==21 || selected==21.5 || selected==22,
              "planner chooses a device-supported grid value");
        if(selected!=0) {
            if(first_heat==0)first_heat=selected;
            else varied=varied || selected!=first_heat;
        }
        const Step step=advance_setpoint(p,room,horizon[i],selected);
        room=step.next;
        cost+=step.cost_eur;
        check(room.indoor_c>=20-1e-8 && room.indoor_c<=23+1e-8,
              "predicted room stays inside the same comfort limits");
    }
    check(varied,"price-responsive schedule uses multiple heating setpoints");
    check(room.indoor_c>=21-1e-8,"matched terminal temperature");
    check(std::fabs(cost-plan.cost_eur)<1e-8,"residential cost replays exactly");
    const auto budget=optimize(p,start,horizon.data(),horizon.size(),1024,control);
    check(budget.status==MpcStatus::ResourceLimit && budget.count==0,"residential search obeys byte budget");
}
int main() {
    room_temperature_uses_the_selected_fresh_source();
    optimize_residential_setpoint_schedule();
    auto p=param();
    check(valid(p),"parameter validation");
    State s{10,false,180};
    Period d{-5,0.22,0,1};
    // Shadow physics must accept finite local-currency prices permitted by
    // the selected market, without broadening EUR-only input limits.
    d.total_price_eur_per_kwh=12.0;
    check(!valid(p,s,d),"EUR range still bounded");
    p.currency_limit_scale=12;
    check(valid(p,s,d),"SEK range accepted");
    p.currency_limit_scale=1;
    d.total_price_eur_per_kwh=0.22;
    auto off=advance(p,s,d,false);
    auto on=advance(p,s,d,true);
    check(on.next.indoor_c>off.next.indoor_c,"heat warms more than off");
    check(off.electric_kwh>=0 && on.electric_kwh>off.electric_kwh,"electric demand");
    check(heat_cop(p,-15)<heat_cop(p,5),"COP declines in cold");
    auto defrost=d;defrost.defrost_capacity_factor=0.1;
    check(advance(p,s,defrost,true).next.indoor_c<on.next.indoor_c,
        "defrost must reduce available heat");
    p.minimum_cop=std::numeric_limits<double>::quiet_NaN();
    check(!valid(p),"nonfinite COP rejected");
    p=param();
    for(size_t horizon: {size_t(96),size_t(192)}) {
        std::vector<Period> periods(horizon);
        for(size_t i=0;i<horizon;i++) {
            const double outside=-4+2*std::sin(6.283185307179586*i/96);
            const double price=(i%96<20 || i%96>=80)?0.07:0.41;
            periods[i]={outside,price,0,1};
        }
        const auto t0=std::chrono::steady_clock::now();
        const auto x=optimize(p,s,periods.data(),periods.size());
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-t0).count();
        if(x.status!=MpcStatus::Ok)
            std::fprintf(stderr,"optimizer status=%d horizon=%zu\n",(int)x.status,horizon);
        check_schedule(p,s,periods,x);
        std::printf("PASS %zu slots: cost=%.5f energy=%.3f min=%.2f final=%.2f starts=%u ms=%lld\n",
            horizon,x.cost_eur,x.electric_kwh,x.minimum_c,x.final_c,x.starts,
            static_cast<long long>(ms));
    }
    auto q=p;
    std::vector<Period> f(96,Period{-5,0.1,0,1});
    check(optimize(q,s,nullptr,96).status==MpcStatus::InvalidInput,"missing forecast");
    f[2].total_price_eur_per_kwh=std::numeric_limits<double>::quiet_NaN();
    check(optimize(q,s,f.data(),96).status==MpcStatus::InvalidInput,"bad price");
    f[2].total_price_eur_per_kwh=0.1;
    check(optimize(q,s,f.data(),96,1000).status==MpcStatus::ResourceLimit,"memory budget");
    check(optimize(q,{6,false,180},f.data(),96).status==MpcStatus::UnsafeStart,
        "unsafe starting temperature");
    check(optimize(q,{10,false,180},f.data(),193).status==MpcStatus::InvalidInput,
        "horizon cap");
    for(auto& d:f) d.outdoor_c=-45;
    check(optimize(q,s,f.data(),96).status==MpcStatus::Infeasible,
        "extreme frost with insufficient heater must be declared infeasible");
    std::puts("ALL COTTAGE TESTS PASSED");
}
