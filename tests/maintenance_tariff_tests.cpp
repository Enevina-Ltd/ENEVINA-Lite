#include "cottage/maintenance_tariff.hpp"
#include "cottage/price_bootstrap.hpp"
#include "cottage/mitsubishi_control_policy.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace enevina::cottage;
int main(){
    constexpr int64_t now=1800000000; // quarter boundary
    std::array<Spot15,96> p{};
    auto set=[&](double a,double b,size_t change) {
        for(size_t i=0;i<p.size();++i)p[i]={now+900*static_cast<int64_t>(i),i<change?a:b};
    };
    MaintenanceTariffScratch scratch{};
    std::array<enevina::OutdoorPoint,25> weather{};
    for(unsigned i=0;i<weather.size();++i)weather[i]={now+int64_t(i)*3600,-2};
    MaintenanceTariffInput in{};in.scratch=&scratch;in.weather=weather.data();in.weather_count=weather.size();
    in.now=now;in.room_c=13.5;in.target_c=12;in.lower_c=11.5;
    in.storage_ceiling_c=15.8;in.cycle_ceiling_c=15.8;in.outside_c=-2;
    in.prices=p.data();in.count=p.size();in.minutes_in_mode=30;in.samples=37;in.off_samples=25;
    in.model.plausible=true;in.model.tau_h=3.2;in.model.tau_std_h=1.0;
    in.model.gain_rise_k=8.1;in.model.lift_k_ref=42;in.model.lift_std_k=13;
    in.model.start_delay_min=3.3;in.model.one_step_sigma_c=.18;in.model.afterheat_k=.5;
    set(.5,.5,96);
    auto a=maintenance_tariff_decision(in);
    assert(a.valid && a.model_based && a.action==MaintenanceTariffAction::None);
    set(.15,.55,1);
    a=maintenance_tariff_decision(in);
    assert(a.action==MaintenanceTariffAction::Preheat && a.ceiling_c==15.8);
    assert(a.alternative_utc>now && a.alternative_price>a.current_price);
    set(.25,.50,8);
    assert(maintenance_tariff_decision(in).action==MaintenanceTariffAction::None);
    in.heating=true;in.room_c=14;
    set(.55,.15,1);
    a=maintenance_tariff_decision(in);
    assert(a.action==MaintenanceTariffAction::Coast && a.ceiling_c>=12 && a.ceiling_c<=14);
    assert(a.avoid_exploration);
    BootstrapInput b{};b.indoor_fresh=true;b.indoor_c=14;b.desired_room_c=12;
    b.low_temperature_cycle=true;b.maintenance_storage=true;b.maintenance_ceiling_c=a.ceiling_c;
    b.maintenance_tariff_coast=true;
    double prices[8]={.5,.25,.25,.25,.25,.25,.25,.25};
    b.next_total_prices=prices;b.price_count=8;b.prices_contiguous_and_fresh=true;
    auto d=bootstrap_price_decision(b);
    assert(d.action==BootstrapAction::Off && d.reason==BootstrapReason::ExpensiveWindow);
    assert(!mode_change_allowed(ControlStep::SetOff,7*60000u,30*60000u,d,true));
    assert(mode_change_allowed(ControlStep::SetOff,15*60000u,30*60000u,d,true));
    set(.5,.25,48);
    a=maintenance_tariff_decision(in);
    assert(a.action==MaintenanceTariffAction::None && a.avoid_exploration);
    // Floor heat remains mandatory even at the highest tariff.
    in.room_c=11.49;assert(maintenance_tariff_decision(in).action==MaintenanceTariffAction::None);
    b.indoor_c=11.49;b.maintenance_tariff_coast=false;b.maintenance_ceiling_c=15.8;
    assert(bootstrap_price_decision(b).action==BootstrapAction::HeatSetpoint);
    // A real headroom limit still wins over cheap-price preheat.
    b.indoor_c=13;b.maintenance_ceiling_c=13;b.maintenance_tariff_preheat=true;
    b.comfort_upper_c=13;
    assert(bootstrap_price_decision(b).action==BootstrapAction::Off);
    // Without a validated thermal model, no long-range coasting estimate.
    in.room_c=13.5;in.heating=false;in.model.plausible=false;
    set(.25,.50,1);
    a=maintenance_tariff_decision(in);
    assert(!a.model_based && a.action==MaintenanceTariffAction::None);
    // Missing data and malformed prices do not produce a speculative plan.
    in.count=1;assert(!maintenance_tariff_decision(in).valid);
    in.count=p.size();p[0].eur_per_kwh=NAN;assert(!maintenance_tariff_decision(in).valid);
    set(.25,.50,2);p[1].utc+=900;
    assert(!maintenance_tariff_decision(in).valid);
    // Charging and terminal restoration use the fitted outdoor-dependent lift.
    TariffCycleSolver solver{in,scratch};
    solver.n=96;solver.tau=2;solver.decay=std::exp(-5.f/120.f);
    solver.ceiling=15.8f;solver.floor=11.5f;solver.lift=40;solver.delay=3;
    solver.lift_ref=-2;solver.lift_slope=1.5f;solver.gain=0;
    for(unsigned j=0;j<solver.n;++j){scratch.outdoor[j]=-10;scratch.price[j]=.25f;}
    assert(std::abs(solver.heating_lift(0)-28)<.001f);
    const auto cold=solver.charge(0,12);
    const auto cold_terminal=solver.terminal(11.5f);
    solver.lift_slope=0;
    const auto warm=solver.charge(0,12);
    assert(cold.valid && warm.valid && cold.cost>warm.cost);
    assert(cold_terminal>solver.terminal(11.5f));
    // End-of-step cooling can be much greater than 0.25 C.
    assert(solver.off_node_temperature(1)<15.55f);
    assert(solver.off_node_temperature(warm.end)<=warm.room+.0001f);
    assert(solver.off_node_temperature(cold.end)<=cold.room+.0001f);
    std::cout<<"PASS forecast-aware preheat, reachable cheaper window, plateau, floor, dwell and missing evidence\n";
}
