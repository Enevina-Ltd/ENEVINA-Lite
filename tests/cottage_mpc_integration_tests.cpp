// Integration of the calibrated MPC (PR #63) with the optimisation-profile
// settings (#57): the owner's hard comfort limits and the price controller used
// as fallback.
#include "cottage/air_humidity.hpp"
#include "cottage/building_identification.hpp"
#include "cottage/comfort_transaction.hpp"
#include "cottage/mpc_arbiter.hpp"
#include "cottage/optimization_profile.hpp"
#include "cottage/price_bootstrap.hpp"
#include "cottage/site_settings.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace enevina::cottage;

namespace {
int failures=0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); ++failures; } } while(0)

constexpr int64_t now0=1768172400;

// The price controller exactly as the firmware feeds it (flat prices: only the
// hard limits and the thermostat band matter here).
BootstrapDecision controller(OptimizationProfile profile,double target,double lo,double hi,double indoor) {
    BootstrapInput in{};
    in.indoor_fresh=true;in.indoor_c=indoor;in.desired_room_c=target;
    size_t window=8;
    if(!apply_profile(in,profile,lo,hi,0.10,window)) return BootstrapDecision{};
    static std::vector<double> prices;
    prices.assign(16,0.10);
    in.next_total_prices=prices.data();in.price_count=window<16?window:16;
    in.prices_contiguous_and_fresh=true;
    return bootstrap_price_decision(in);
}

void test_settings_and_mpc_agree_on_valid_limits() {
    unsigned both=0,mpc_only_invalid=0;
    for(double target=5.0;target<=15.5+1e-9;target+=0.5) {
        for(double lo=0;lo<=15.0;lo+=(lo==0?5.0:0.5)) {
            for(double hi=0;hi<=26.0;hi+=(hi==0?6.0:0.5)) {
                SiteSettings site{};
                site.comfort_min_c=lo;site.comfort_max_c=hi;
                CottageModeConfig mode{};
                mode.mode=CottageMode::Hold;mode.hold_target_c=target;
                const bool settings_ok=valid_comfort_limits(lo,hi) &&
                    mode_comfort_limits_fit(site,mode);
                ComfortBand band{};
                const bool mpc_ok=mpc_comfort_band(target,band,ComfortLimits{lo,hi});
                if(settings_ok) {
                    // Everything the owner may legitimately save gives the MPC a band ...
                    CHECK(mpc_ok);
                    if(!mpc_ok) continue;
                    ++both;
                    // ... that is never looser than the owner's limits.
                    if(lo>0) CHECK(band.lower_c>=lo-1e-9);
                    if(hi>0) CHECK(band.upper_c<=hi+1e-9);
                    CHECK(band.lower_c>=target-0.5-1e-9 && band.upper_c<=target+1.0+1e-9);
                    CHECK(band.target_c==target && band.lower_c<=band.target_c && band.target_c<band.upper_c);
                } else {
                    // Limits the settings layer refuses can never become a MPC band.
                    CHECK(!mpc_ok);
                    ++mpc_only_invalid;
                }
            }
        }
    }
    CHECK(both>500 && mpc_only_invalid>500);
}

void test_review_example_13_with_12_and_13_5() {
    // Target 13.0, owner min 12.0, max 13.5: the MPC must not plan 14.0 C.
    ComfortBand b{};
    CHECK(mpc_comfort_band(13.0,b,ComfortLimits{12.0,13.5}));
    CHECK(std::fabs(b.upper_c-13.5)<1e-9 && std::fabs(b.lower_c-12.5)<1e-9);
    SiteSettings site{};site.comfort_min_c=12.0;site.comfort_max_c=13.5;
    CottageModeConfig mode{};mode.mode=CottageMode::Hold;mode.hold_target_c=13.0;
    CHECK(mode_comfort_limits_fit(site,mode));
}

void test_fallback_controller_obeys_the_same_limits() {
    for(OptimizationProfile profile:{OptimizationProfile::Comfort,OptimizationProfile::Balanced,
                                      OptimizationProfile::Saving}) {
        for(double indoor=11.0;indoor<=15.0;indoor+=0.1) {
            const auto d=controller(profile,13.0,12.0,13.5,indoor);
            if(indoor>=13.5) CHECK(d.action==BootstrapAction::Off);          // never heats past the owner's max
            if(indoor<=12.0) CHECK(d.action==BootstrapAction::HeatSetpoint); // never idles below the owner's min
        }
    }
}

// A plan the MPC made under the owner's limits; the arbiter then falls back
// to exactly what the price controller says whenever the MPC is not allowed.
ArbiterInput armed_input(const ComfortLimits& limits,bool first_heat,double indoor,double target=13.0) {
    ArbiterInput in{};
    in.now_utc=now0+40;in.temperature_fresh=true;in.indoor_c=indoor;
    in.calibrated=true;in.calibration=CalibrationState::Calibrated;
    MpcPlanRecord& p=in.plan;
    p.has_plan=true;p.status=MpcStatus::Ok;p.count=96;
    p.first_utc=now0;p.computed_utc=now0+20;
    p.initial_heat=false;p.first_heat=first_heat;p.target_c=target;p.limits=limits;
    mpc_comfort_band(target,p.band,limits);
    in.limits=limits;
    in.pump_state_known=true;in.pump_heat=false;
    in.bootstrap=controller(OptimizationProfile::Balanced,target,limits.min_c,limits.max_c,indoor);
    in.bootstrap.requested_room_c=target;
    return in;
}

void test_mpc_never_overrides_owner_limits_in_the_chain() {
    const ComfortLimits lim{12.0,13.5};
    // Room reaches the owner's max while the plan says HEAT: price control (Off).
    {   auto in=armed_input(lim,true,13.5);
        const auto o=arbitrate(in);
        CHECK(o.source==ProposalSource::PriceControl && o.fallback==MpcFallback::AboveComfortCeiling);
        CHECK(o.proposal.action==BootstrapAction::Off); }
    // Just inside: MPC heats.
    {   auto in=armed_input(lim,true,13.4);
        CHECK(arbitrate(in).source==ProposalSource::Mpc); }
    // Room at the planned floor while the plan says OFF: price control heats.
    {   auto in=armed_input(lim,false,12.5);
        const auto o=arbitrate(in);
        CHECK(o.source==ProposalSource::PriceControl && o.fallback==MpcFallback::BelowComfortFloor);
        CHECK(o.proposal.action==BootstrapAction::HeatSetpoint || o.proposal.action==BootstrapAction::Hold); }
    // Owner changes the limits mid-plan: the plan is revoked, price control decides.
    {   auto in=armed_input(lim,true,13.0);
        in.limits=ComfortLimits{12.0,13.0 + 0.5};      // same max -> still valid
        CHECK(arbitrate(in).source==ProposalSource::Mpc);
        in.limits=ComfortLimits{12.5,14.0};
        const auto o=arbitrate(in);
        CHECK(o.fallback==MpcFallback::PlanInputsChanged && o.source==ProposalSource::PriceControl); }
    // Limits the settings layer would never allow: no MPC, controller's decision stands.
    {   auto in=armed_input(lim,true,13.0);
        in.limits=ComfortLimits{13.0,13.5};
        const auto o=arbitrate(in);
        CHECK(o.fallback==MpcFallback::ComfortLimitsInvalid && o.source==ProposalSource::PriceControl); }
    // Thermostat when prices are missing, still bounded by the same limits.
    {   auto in=armed_input(lim,true,13.5);
        in.bootstrap.price_data_valid=false;
        in.limits=ComfortLimits{14.0,12.0};            // contradictory
        const auto o=arbitrate(in);
        CHECK(o.source==ProposalSource::Thermostat && o.fallback==MpcFallback::ComfortLimitsInvalid); }
}

} // namespace

int main() {
    test_settings_and_mpc_agree_on_valid_limits();
    test_review_example_13_with_12_and_13_5();
    test_fallback_controller_obeys_the_same_limits();
    test_mpc_never_overrides_owner_limits_in_the_chain();
    if(failures) {std::printf("%d integration check(s) failed\n",failures);return 1;}
    std::puts("cottage_mpc_integration_tests: all passed");
    return 0;
}
