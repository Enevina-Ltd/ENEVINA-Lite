// Optimization profiles on the live price-aware room controller.
#include "cottage/optimization_profile.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace enevina::cottage;
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)
static bool near(double a,double b){return std::fabs(a-b)<1e-9;}

static const double kCheapNow[16]={0.05,0.06,0.07,0.08,0.20,0.21,0.22,0.23,0.24,0.25,0.26,0.27,0.28,0.29,0.30,0.31};
static const double kDearNow[16]={0.40,0.41,0.39,0.38,0.20,0.21,0.22,0.23,0.24,0.25,0.26,0.27,0.28,0.29,0.30,0.31};
static const double kFlat[16]={0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20,0.20};

static BootstrapInput base(double indoor,double desired,const double* prices,size_t n=8) {
    BootstrapInput in{};in.heating={16,26,0.5,true,true};
    in.indoor_fresh=true;in.indoor_c=indoor;in.desired_room_c=desired;
    in.next_total_prices=prices;in.price_count=n;in.prices_contiguous_and_fresh=true;
    return in;
}
static BootstrapDecision decide(OptimizationProfile p,double indoor,double desired,double lo,double hi,
                                const double* prices) {
    size_t window=0;
    BootstrapInput in=base(indoor,desired,prices);
    CHECK(apply_profile(in,p,lo,hi,0.10,window));
    in.price_count=window<=16?window:16;
    return bootstrap_price_decision(in);
}

static void names_and_limits() {
    OptimizationProfile p{};
    for(auto x:{OptimizationProfile::Comfort,OptimizationProfile::Balanced,OptimizationProfile::Saving}) {
        CHECK(parse_profile(profile_name(x),p)&&p==x);
    }
    CHECK(!parse_profile("eco",p)&&!parse_profile("",p)&&!parse_profile(nullptr,p));
    CHECK(valid_comfort_limits(0,0)&&valid_comfort_limits(19,0)&&valid_comfort_limits(0,23));
    CHECK(valid_comfort_limits(19,23)&&valid_comfort_limits(19.5,20.5));
    CHECK(!valid_comfort_limits(4.5,0)&&!valid_comfort_limits(0,26.5));        // outside 5..26
    CHECK(!valid_comfort_limits(19.3,0));                                       // 0.5 steps only
    CHECK(!valid_comfort_limits(20,20.5)&&!valid_comfort_limits(23,19));        // >= 1 C apart, ordered
    CHECK(!valid_comfort_limits(std::numeric_limits<double>::quiet_NaN(),0));
    CHECK(profile_tuning(OptimizationProfile::Comfort).headroom_share==0);
    CHECK(profile_tuning(OptimizationProfile::Saving).window_slots>profile_tuning(OptimizationProfile::Balanced).window_slots);
}

// Balanced without an owner band is exactly the behaviour before profiles.
static void default_is_unchanged() {
    size_t checks=0;
    for(const double* prices:{kCheapNow,kDearNow,kFlat}) {
        for(double desired:{12.0,16.0,21.0}) {
            for(double indoor=desired-2;indoor<=desired+2;indoor+=0.1) {
                BootstrapInput raw=base(indoor,desired,prices);
                BootstrapInput prof=base(indoor,desired,prices);
                size_t window=0;
                CHECK(apply_profile(prof,OptimizationProfile::Balanced,0,0,0.10,window)&&window==8);
                const auto a=bootstrap_price_decision(raw),b=bootstrap_price_decision(prof);
                CHECK(a.action==b.action&&a.reason==b.reason&&near(a.heat_setpoint_c,b.heat_setpoint_c));
                ++checks;
            }
        }
    }
    CHECK(checks>300);
}

static void saving_uses_the_band_comfort_does_not() {
    // Target 21 C, owner allows 19..23 C.
    // Cheap electricity, room at 22.0 (above target): Comfort stops heat at its ceiling, Saving may store it.
    CHECK(decide(OptimizationProfile::Comfort,22.0,21,19,23,kCheapNow).action==BootstrapAction::Off);
    const auto bal=decide(OptimizationProfile::Balanced,21.5,21,19,23,kCheapNow);
    CHECK(bal.action==BootstrapAction::HeatSetpoint&&bal.reason==BootstrapReason::CheapWindow);   // 40 % = 0.8 C
    CHECK(decide(OptimizationProfile::Balanced,21.9,21,19,23,kCheapNow).action==BootstrapAction::Off);
    // Saving acts 0.75 C inside the limits (working band 19.75..22.25) and uses 80 % of it.
    const auto sav=decide(OptimizationProfile::Saving,21.8,21,19,23,kCheapNow);
    CHECK(sav.action==BootstrapAction::HeatSetpoint&&sav.reason==BootstrapReason::CheapWindow);
    CHECK(sav.heat_setpoint_c>=22.5-1e-9);                          // setpoint reaches the stored-heat target
    CHECK(decide(OptimizationProfile::Saving,22.1,21,19,23,kCheapNow).action==BootstrapAction::Off);
    // Expensive now, room at 20.0 (below target): Comfort actively restores temperature, Saving can coast.
    CHECK(decide(OptimizationProfile::Comfort,20.0,21,19,23,kDearNow).action==BootstrapAction::HeatSetpoint);
    const auto coast=decide(OptimizationProfile::Saving,20.2,21,19,23,kDearNow);
    CHECK(coast.action==BootstrapAction::Off&&coast.reason==BootstrapReason::ExpensiveWindow);
    CHECK(decide(OptimizationProfile::Saving,19.9,21,19,23,kDearNow).action==BootstrapAction::HeatSetpoint); // profile coasting exhausted
    CHECK(decide(OptimizationProfile::Balanced,20.5,21,19,23,kDearNow).action==BootstrapAction::Off);
    CHECK(decide(OptimizationProfile::Balanced,20.0,21,19,23,kDearNow).action==BootstrapAction::HeatSetpoint);  // beyond 40 % coasting room
}

// Hard comfort limits hold for every profile, price and temperature.
static void limits_are_never_priced_away() {
    size_t checked=0;
    for(auto prof:{OptimizationProfile::Comfort,OptimizationProfile::Balanced,OptimizationProfile::Saving}) {
        for(const double* prices:{kCheapNow,kDearNow,kFlat}) {
            for(double indoor=17.0;indoor<=25.0;indoor+=0.05) {
                const auto d=decide(prof,indoor,21,19,23,prices);
                if(indoor<=19.0) CHECK(d.action==BootstrapAction::HeatSetpoint);
                if(indoor>=23.0) CHECK(d.action==BootstrapAction::Off);
                CHECK(!d.executable);           // a profile never authorizes a command
                ++checked;
            }
        }
    }
    CHECK(checked>1000);
    // Narrow band is the floor of the limits: a limit tighter than +-0.5 C is ignored.
    size_t window=0;
    BootstrapInput in=base(20.8,21,kFlat);
    CHECK(apply_profile(in,OptimizationProfile::Saving,21.0,0,0.10,window));   // above target-0.5
    CHECK(in.comfort_lower_c==0&&in.coast_c==0);
    // A limit on the wrong side of the target is dropped, not inverted.
    in=base(21,21,kFlat);
    CHECK(apply_profile(in,OptimizationProfile::Saving,22,0,0.10,window));
    CHECK(in.comfort_lower_c==0);
    CHECK(apply_profile(in,OptimizationProfile::Saving,0,19,0.10,window));
    CHECK(in.comfort_upper_c==0&&in.preheat_c==0);
}

static void no_prices_is_a_plain_thermostat() {
    // Network outage: the narrow band around the target, whatever the profile.
    for(auto prof:{OptimizationProfile::Comfort,OptimizationProfile::Balanced,OptimizationProfile::Saving}) {
        size_t window=0;
        BootstrapInput in=base(21.6,21,nullptr);
        in.prices_contiguous_and_fresh=false;in.price_count=0;
        CHECK(apply_profile(in,prof,19,23,0.10,window));
        CHECK(bootstrap_price_decision(in).action==BootstrapAction::Off);          // >= target+0.5
        in.indoor_c=20.4;
        CHECK(bootstrap_price_decision(in).action==BootstrapAction::HeatSetpoint); // <= target-0.5
        in.indoor_c=21.0;
        CHECK(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    }
}

static void profile_thresholds_stop_and_resume_commands() {
    // Comfort is never allowed to coast from 21 all the way down to the 19 C
    // hard limit. Saving must reheat at its own smaller coasting threshold.
    const auto c=decide(OptimizationProfile::Comfort,20.5,21,19,23,kDearNow);
    CHECK(c.action==BootstrapAction::HeatSetpoint);
    const auto upper=decide(OptimizationProfile::Saving,23.0,21,19,23,kCheapNow);
    CHECK(upper.action==BootstrapAction::Off);
    const auto lower=decide(OptimizationProfile::Saving,19.9,21,19,23,kDearNow);
    CHECK(lower.action==BootstrapAction::HeatSetpoint);
}
static void price_threshold_scales_with_profile() {
    // Prices differ from the median by ~15 %: Saving (5 %) and Balanced (10 %) act, Comfort (20 %) does not.
    static const double mild[8]={0.17,0.20,0.20,0.20,0.20,0.20,0.20,0.20};   // now 15 % below median 0.20
    size_t window=0;
    for(auto prof:{OptimizationProfile::Comfort,OptimizationProfile::Balanced,OptimizationProfile::Saving}) {
        BootstrapInput in=base(20.8,21,mild);
        CHECK(apply_profile(in,prof,0,0,0.10,window));
        in.price_count=8;
        const auto d=bootstrap_price_decision(in);
        const bool acts=d.reason==BootstrapReason::CheapWindow;
        CHECK(acts==(prof!=OptimizationProfile::Comfort));
    }
}

static void wide_owner_band_never_disables_the_controller() {
    // Codex P1 (#57): a valid 5..26 C band around a 12 C target produced
    // preheat 7 C / coast 4.. beyond the controller's malformed-input limits,
    // so a fresh reading returned Unavailable and the heating never ran.
    static const double bands[][2]={{5,26},{5,0},{0,26},{5,12.5},{11.5,26},{19,23},{0,0}};
    int checked=0;
    for(auto prof:{OptimizationProfile::Comfort,OptimizationProfile::Balanced,OptimizationProfile::Saving})
    for(double target=5;target<=26;target+=0.5)
    for(const auto& b:bands) {
        if(!valid_comfort_limits(b[0],b[1])) continue;
        for(double indoor=3;indoor<=30;indoor+=0.5) {
            size_t window=0;
            BootstrapInput in=base(indoor,target,kCheapNow);
            if(!apply_profile(in,prof,b[0],b[1],0.10,window)) continue;
            in.price_count=window<=16?window:16;
            CHECK(in.preheat_c<=kMaxPreheatC&&in.coast_c<=kMaxCoastC);
            CHECK(bootstrap_price_decision(in).action!=BootstrapAction::Unavailable);
            ++checked;
        }
    }
    CHECK(checked>5000);
    // the real case from the review, spelled out
    size_t window=0;
    BootstrapInput in=base(11,12,kCheapNow);
    CHECK(apply_profile(in,OptimizationProfile::Saving,5,26,0.10,window));
    in.price_count=window<=16?window:16;
    CHECK(bootstrap_price_decision(in).action!=BootstrapAction::Unavailable);
}

static void malformed_input_is_refused() {
    size_t window=0;
    BootstrapInput in=base(21,21,kFlat);
    CHECK(!apply_profile(in,OptimizationProfile::Saving,19,18,0.10,window));      // inverted band
    CHECK(!apply_profile(in,OptimizationProfile::Saving,0,0,-0.1,window));
    CHECK(!apply_profile(in,OptimizationProfile::Saving,0,0,std::numeric_limits<double>::quiet_NaN(),window));
    in.preheat_c=std::numeric_limits<double>::quiet_NaN();
    CHECK(bootstrap_price_decision(in).action==BootstrapAction::Unavailable);
}

#include "profile_sim.hpp"
#ifndef ENEVINA_TEST_DATA_DIR
#error "ENEVINA_TEST_DATA_DIR must point at tests/data"
#endif
// Real recorded market prices, SYNTHETIC building/weather/pump.
// A lower raw cost can simply be colder stored building mass at the horizon:
// compare only after charging a common terminal thermal-energy equivalent.
// This is NOT a measured saving and cannot prove a physically feasible
// recovery trajectory; no fixed annual saving or cost-ordering is asserted.
static void simulation_on_real_prices() {
    struct Z {const char* file;double addon;};
    const Z zones[]={{"FI_EUR_2026-11-12_to_18.csv",0.06},{"DK1_EUR_2026-11-12_to_18.csv",0.06},
                     {"SE3_SEK_2026-11-12_to_18.csv",0.70}};
    const profile_sim::Building building{};
    for(const auto& z:zones) {
        profile_sim::Series s;
        CHECK(profile_sim::load_series(std::string(ENEVINA_TEST_DATA_DIR)+"/prices/"+z.file,s));
        if(s.per_mwh.size()<32)continue;
        const auto baseline=profile_sim::simulate(s,z.addon,OptimizationProfile::Comfort,19,23,false,building);
        const auto comfort=profile_sim::simulate(s,z.addon,OptimizationProfile::Comfort,19,23,true,building);
        const auto balanced=profile_sim::simulate(s,z.addon,OptimizationProfile::Balanced,19,23,true,building);
        const auto saving=profile_sim::simulate(s,z.addon,OptimizationProfile::Saving,19,23,true,building);
        const profile_sim::Outcome outcomes[]={baseline,comfort,balanced,saving};
        double common_c=outcomes[0].final_c;
        for(const auto& run:outcomes) {
            CHECK(run.min_c>=19.0&&run.max_c<=23.0);
            CHECK(run.minutes_outside_limits==0);
            CHECK(std::isfinite(run.final_c)&&std::isfinite(run.cost));
            common_c=std::max(common_c,run.final_c);
        }
        double average_recovery_price=0;
        for(size_t j=s.per_mwh.size()-16;j<s.per_mwh.size();++j)
            average_recovery_price+=s.per_mwh[j]/1000.0+z.addon;
        average_recovery_price/=16.0;
        const double recovery_cop=heat_cop(profile_sim::pump(),profile_sim::outdoor_c(s.per_mwh.size()-1));
        for(size_t i=0;i<4;++i) {
            const auto adjusted=profile_sim::equalize_terminal(outcomes[i],building,
                common_c,average_recovery_price,recovery_cop);
            CHECK(std::isfinite(adjusted.equalized_cost));
            CHECK(std::fabs(outcomes[i].final_c*building.mass_kwh_per_c+
                 adjusted.thermal_kwh-common_c*building.mass_kwh_per_c)<1e-7);
            // Present both figures so cold-end depletion cannot masquerade as
            // energy savings; whether a profile actually saves is an outcome.
            std::printf("TERMINAL_AUDIT %s profile=%zu raw=%.4f terminal_c=%.3f debt_kwh=%.3f equalized=%.4f\n",
                z.file,i,outcomes[i].cost,outcomes[i].final_c,
                adjusted.thermal_kwh,adjusted.equalized_cost);
        }
    }
}
static void terminal_energy_normalization_math() {
    profile_sim::Outcome cold{};cold.final_c=19.5;cold.cost=4.0;
    const profile_sim::Building building{6.0,0.12,21.0,21.0};
    auto adjusted=profile_sim::equalize_terminal(cold,building,21.0,0.2,3.0);
    CHECK(std::fabs(adjusted.thermal_kwh-9.0)<1e-9);
    CHECK(std::fabs(adjusted.electric_kwh-3.0)<1e-9);
    CHECK(std::fabs(adjusted.equalized_cost-4.6)<1e-9);
}

int main() {
    simulation_on_real_prices();
    terminal_energy_normalization_math();
    names_and_limits();
    default_is_unchanged();
    saving_uses_the_band_comfort_does_not();
    limits_are_never_priced_away();
    no_prices_is_a_plain_thermostat();
    profile_thresholds_stop_and_resume_commands();
    price_threshold_scales_with_profile();
    wide_owner_band_never_disables_the_controller();
    malformed_input_is_refused();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("optimization_profile_tests OK");
    return 0;
}
