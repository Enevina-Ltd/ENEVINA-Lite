// Building identification: parameter recovery, honest calibration states,
// meter-scope rule, walk-forward validation, degradation and persistence.
#include "cottage/building_identification.hpp"
#include "tests/cottage_closed_loop.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace enevina::cottage;
using closed_loop::Rng;

namespace {
int failures=0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); ++failures; } } while(0)

constexpr int64_t t_start=1768168800-30*86400;

struct Sim {
    Parameters truth=closed_loop::true_cottage();
    double gain_kw=0.1, solar_kw=0.0, noise_c=0.03, forecast_err_c=0.3;
    double target=12.0;
    bool pump_meter=false;
    bool always_off=false;
    State st{12.0,false,120};
    int64_t utc=t_start;
    Rng rng{42};
    double meas=12.0;
    unsigned since=120;
    double outdoor(int64_t t) const {
        const double h=((t+7200)%86400)/3600.0;
        return -3.0+4.0*std::sin(6.283185307179586*(h-9)/24.0)+
            3.0*std::sin(6.283185307179586*double(t-t_start)/(86400.0*4.3));
    }
    SlotObservation step() {
        bool want=st.heat_requested;
        if(always_off) want=false;
        else if(meas<=target-0.5) want=true;
        else if(meas>=target+0.5) want=false;
        if(want!=st.heat_requested && since<30) want=st.heat_requested;
        const double to=outdoor(utc);
        const double solar=solar_kw*std::max(0.0,std::sin(6.283185307179586*
            (((utc+7200)%86400)/86400.0-0.25)));
        const Period d{to,0.1,gain_kw+solar,1.0};
        SlotObservation o{};
        o.slot_utc=utc;
        o.indoor_valid=true;
        o.indoor_start_c=meas;
        const Step s=advance(truth,st,d,want);
        since=want!=st.heat_requested?15:std::min(600u,since+15);
        st=s.next;
        meas=std::round((st.indoor_c+noise_c*rng.normal())*100.0)/100.0;
        o.indoor_end_c=meas;
        o.outdoor_valid=true;
        o.outdoor_c=to+forecast_err_c*rng.normal();
        o.heat_valid=true;o.heat_requested=want;o.in_model_domain=true;
        o.electric_valid=true;o.electric_kwh=s.electric_kwh*(1.0+0.02*rng.normal());
        o.meter_pump_only=pump_meter;
        utc+=900;
        return o;
    }
};
double rel(double a,double b){return std::fabs(a-b)/std::fabs(b);}

void test_no_data_is_honest() {
    BuildingIdentifier id;
    const auto r=id.report(t_start);
    CHECK(r.state==CalibrationState::NoData);
    CHECK(r.samples==0);
    CHECK(!r.model.plausible);
    CHECK(!r.model.cop_identified);
    Parameters p{};bool kwh=true;
    ComfortBand band{};CHECK(mpc_comfort_band(12.0,band));
    CHECK(id.build_parameters(t_start,band,true,1.0,p,kwh)==ModelBuildError::NotCalibrated);
    CHECK(!kwh);
}

void test_comfort_band() {
    ComfortBand b{};
    CHECK(mpc_comfort_band(12.0,b) && std::fabs(b.lower_c-11.5)<1e-9 && std::fabs(b.upper_c-13.0)<1e-9);
    CHECK(mpc_comfort_band(15.0,b) && std::fabs(b.upper_c-15.8)<1e-9);
    CHECK(!mpc_comfort_band(21.0,b));   // occupied: outside HEAT 16 C model
    CHECK(!mpc_comfort_band(16.0,b));
    CHECK(!mpc_comfort_band(NAN,b));
    CHECK(mpc_comfort_band(5.2,b) && b.lower_c>=5.0);
}

void test_recovers_truth_and_calibrates() {
    Sim sim;
    BuildingIdentifier id;
    CalibrationState seen=CalibrationState::NoData;
    bool saw_learning=false,saw_validating=false;
    for(int i=0;i<12*96;++i) {
        id.add(sim.step());
        seen=id.report(sim.utc).state;
        saw_learning|=seen==CalibrationState::Learning;
        saw_validating|=seen==CalibrationState::Validating;
    }
    const auto r=id.report(sim.utc);
    std::printf("tau=%.2f+-%.2f h lift_ref=%.2f+-%.2f K @%.1fC lift0=%.2f delay=%.2f+-%.2f min after=%.3f K "
                "sigma=%.3f rms=%.3f bias=%.3f windows=%u state=%s block=%s\n",
        r.model.tau_h,r.model.tau_std_h,r.model.lift_k_ref,r.model.lift_std_k,
        r.model.lift_ref_outdoor_c,r.model.lift_k_at_0c,
        r.model.start_delay_min,r.model.start_delay_std_min,r.model.afterheat_k,
        r.model.one_step_sigma_c,r.validation_rms_c,r.validation_bias_c,r.windows,
        calibration_state_name(r.state),calibration_block_name(r.block));
    CHECK(saw_learning && saw_validating);
    CHECK(r.state==CalibrationState::Calibrated);
    const double tau_true=sim.truth.thermal_mass_kwh_per_c/sim.truth.loss_kw_per_c;
    CHECK(rel(r.model.tau_h,tau_true)<0.15);
    const double to=r.model.lift_ref_outdoor_c;
    const double lift_true=heat_capacity_kw(sim.truth,to)/sim.truth.loss_kw_per_c;
    CHECK(rel(r.model.lift_k_ref,lift_true)<0.15);
    CHECK(r.model.start_delay_min>=0 && r.model.start_delay_min<=9);
    CHECK(r.model.afterheat_k>=0);
    CHECK(r.model.tau_std_h>0 && r.model.lift_std_k>0);
    CHECK(!r.model.electric_trusted);       // no pump-only meter confirmed
    CHECK(!r.model.absolute_bounds && !r.model.cop_identified);
    // Normalised parameters predict like the truth (open loop, 8 h).
    ComfortBand band{};CHECK(mpc_comfort_band(12.0,band));
    Parameters p{};bool kwh=true;
    CHECK(id.build_parameters(sim.utc,band,false,1.0,p,kwh)==ModelBuildError::None);
    CHECK(!kwh && valid(p));
    State a{12.0,false,120},b=a;
    double worst=0;
    for(int k=0;k<32;++k) {
        const bool on=(k/6)%2==0;
        const double o=-2.0;
        const Period dt{o,0.1,sim.gain_kw,1.0};
        const Period dm{o,0.1,r.model.gain_rise_k*p.loss_kw_per_c,1.0};
        a=advance(sim.truth,a,dt,on).next;
        b=advance(p,b,dm,on).next;
        worst=std::max(worst,std::fabs(a.indoor_c-b.indoor_c));
    }
    std::printf("open-loop 8 h model vs truth worst %.3f K\n",worst);
    CHECK(worst<0.6);
    // One normalised electric unit per HEAT slot without a meter.
    const Period dm{-2.0,0.1,0,1.0};
    const auto heat=advance(p,State{10.0,true,120},dm,true);
    CHECK(std::fabs(heat.electric_kwh-slot_hours)<1e-6);
}

void test_no_heating_never_calibrates() {
    Sim sim;sim.always_off=true;sim.st.indoor_c=12;sim.meas=12;
    BuildingIdentifier id;
    for(int i=0;i<6*96;++i) id.add(sim.step());
    const auto r=id.report(sim.utc);
    CHECK(r.state==CalibrationState::Learning);
    CHECK(r.block==CalibrationBlock::NeedHeatingSlots);
}

void test_meter_scope_rule() {
    // Whole-house / unconfirmed meter: electric data is never used.
    {
        Sim sim;sim.pump_meter=false;
        BuildingIdentifier id;
        for(int i=0;i<10*96;++i) id.add(sim.step());
        const auto r=id.report(sim.utc);
        CHECK(r.model.electric_samples==0);
        CHECK(!r.model.electric_trusted);
        ComfortBand band{};mpc_comfort_band(12.0,band);
        Parameters p{};bool kwh=true;
        // Even if the CALLER later claims a pump-only meter, no electric
        // model exists to use.
        if(r.state==CalibrationState::Calibrated) {
            CHECK(id.build_parameters(sim.utc,band,true,1.0,p,kwh)==ModelBuildError::None);
            CHECK(!kwh);
        }
    }
    // Owner-confirmed pump-only meter: electric draw identified.
    {
        Sim sim;sim.pump_meter=true;
        BuildingIdentifier id;
        for(int i=0;i<12*96;++i) id.add(sim.step());
        const auto r=id.report(sim.utc);
        const double p_true=sim.truth.heat_standby_kw-sim.truth.off_standby_kw+
            heat_capacity_kw(sim.truth,0)/heat_cop(sim.truth,0);
        std::printf("electric on@0C=%.3f (true %.3f) slope=%.4f standby=%.4f start=%.3f rel_err=%.3f "
                    "UA>=%.3f C>=%.3f\n",r.model.electric_on_kw_at_0c,p_true,
                    r.model.electric_on_kw_per_c,r.model.electric_standby_kw,
                    r.model.electric_start_kwh,r.model.electric_rel_error,
                    r.model.ua_lower_kw_per_c,r.model.capacity_lower_kwh_per_c);
        CHECK(r.model.electric_trusted);
        CHECK(rel(r.model.electric_on_kw_at_0c,p_true)<0.10);
        CHECK(r.model.absolute_bounds);
        // A bound must never exceed the truth (COP >= 1 is conservative).
        CHECK(r.model.ua_lower_kw_per_c<=sim.truth.loss_kw_per_c*1.05);
        CHECK(!r.model.cop_identified);
        CHECK(r.state==CalibrationState::Calibrated);
        ComfortBand band{};mpc_comfort_band(12.0,band);
        Parameters p{};bool kwh=false;
        CHECK(id.build_parameters(sim.utc,band,true,1.0,p,kwh)==ModelBuildError::None);
        CHECK(kwh);
        // Meter confirmation withdrawn -> back to normalised units.
        CHECK(id.build_parameters(sim.utc,band,false,1.0,p,kwh)==ModelBuildError::None);
        CHECK(!kwh);
        // Electric energy of one HEAT slot close to the truth at 0 C.
        CHECK(id.build_parameters(sim.utc,band,true,1.0,p,kwh)==ModelBuildError::None);
        const Period d{0.0,0.1,0,1.0};
        const double e_model=advance(p,State{10,true,120},d,true).electric_kwh;
        const double e_true=advance(sim.truth,State{10,true,120},d,true).electric_kwh;
        CHECK(rel(e_model,e_true)<0.10);
    }
}

void test_rejects_replay_gap_and_outlier() {
    Sim sim;
    BuildingIdentifier id;
    std::vector<SlotObservation> obs;
    for(int i=0;i<200;++i) obs.push_back(sim.step());
    for(const auto& o:obs) id.add(o);
    const auto before=id.report(sim.utc);
    CHECK(!id.add(obs.back()));               // duplicate slot
    CHECK(!id.add(obs[10]));                  // replay of older slot
    CHECK(id.report(sim.utc).revision==before.revision);
    SlotObservation gap=sim.step();gap.slot_utc+=1800;sim.utc+=1800;
    CHECK(!id.add(gap));                      // non-contiguous: no start/stop knowledge
    SlotObservation bad=sim.step();bad.slot_utc+=0;bad.slot_utc=0;
    CHECK(!id.add(bad));
    // Recover continuity, then a 2.5 K sensor jump is an outlier.
    for(int i=0;i<4;++i) id.add(sim.step());
    SlotObservation spike=sim.step();
    spike.indoor_end_c=spike.indoor_start_c+2.5;
    const uint32_t out_before=id.report(sim.utc).outliers;
    CHECK(!id.add(spike));
    CHECK(id.report(sim.utc).outliers==out_before+1);
    // NaN never reaches the fit.
    SlotObservation n=sim.step();n.indoor_end_c=NAN;
    CHECK(!id.add(n));
    const auto r=id.report(sim.utc);
    CHECK(std::isfinite(r.model.tau_h));
}

void test_wrong_prior_converges() {
    Sim sim;
    BuildingIdentifier id;
    id.set_prior(4*16.67,3*26.7,50.0);
    for(int i=0;i<14*96;++i) id.add(sim.step());
    const auto r=id.report(sim.utc);
    std::printf("wrong prior: tau=%.2f lift_ref=%.2f state=%s\n",r.model.tau_h,
                r.model.lift_k_ref,calibration_state_name(r.state));
    CHECK(r.state==CalibrationState::Calibrated);
    CHECK(rel(r.model.tau_h,16.67)<0.2);
}

void test_degrades_when_building_changes() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<12*96;++i) id.add(sim.step());
    CHECK(id.report(sim.utc).state==CalibrationState::Calibrated);
    // A window left open: losses x3 (the pump can no longer hold the room).
    // The old model must stop being trusted within one day, whichever
    // check notices first (walk-forward error -> DEGRADED, or the refit
    // becoming implausible/uncertain -> LEARNING).
    sim.truth.loss_kw_per_c*=3.0;
    int dropped_after=-1;
    for(int i=0;i<96 && dropped_after<0;++i) {
        id.add(sim.step());
        if(id.report(sim.utc).state!=CalibrationState::Calibrated) dropped_after=i+1;
    }
    std::printf("open window: calibration withdrawn after %d slots (%s)\n",dropped_after,
                calibration_state_name(id.report(sim.utc).state));
    CHECK(dropped_after>0 && dropped_after<=96);
    ComfortBand band{};mpc_comfort_band(12.0,band);
    Parameters p{};bool kwh=false;
    CHECK(id.build_parameters(sim.utc,band,false,1.0,p,kwh)==ModelBuildError::NotCalibrated);
}

void test_moderate_change_degrades() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<12*96;++i) id.add(sim.step());
    // Heat output halves (e.g. iced coil, wrong mode): pump can still hold
    // the room, the fit keeps excitation, the walk-forward error exposes it.
    sim.truth.heat_kw_at_zero_c*=0.5;sim.truth.minimum_heat_kw*=0.5;
    bool trusted=true;
    for(int i=0;i<2*96 && trusted;++i) {
        id.add(sim.step());
        trusted=id.report(sim.utc).state==CalibrationState::Calibrated;
    }
    std::printf("half capacity: state=%s block=%s\n",
        calibration_state_name(id.report(sim.utc).state),
        calibration_block_name(id.report(sim.utc).block));
    CHECK(!trusted);
}

void test_stale_validation() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<12*96;++i) id.add(sim.step());
    CHECK(id.report(sim.utc).state==CalibrationState::Calibrated);
    const auto r=id.report(sim.utc+15*86400);
    CHECK(r.state==CalibrationState::Validating);
    CHECK(r.block==CalibrationBlock::ValidationStale);
    CHECK(id.report(sim.utc-86400*30).state!=CalibrationState::Calibrated); // clock behind
}

void test_persistence() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<12*96;++i) id.add(sim.step());
    std::vector<uint8_t> blob(BuildingIdentifier::blob_capacity);
    size_t used=0;
    CHECK(id.save(0x1234u,blob.data(),blob.size(),used) && used>0 && used<=blob.size());
    BuildingIdentifier copy;
    CHECK(copy.load(0x1234u,blob.data(),used));
    const auto a=id.report(sim.utc),b=copy.report(sim.utc);
    CHECK(a.state==b.state && a.revision==b.revision && a.samples==b.samples);
    CHECK(std::fabs(a.model.tau_h-b.model.tau_h)<1e-12);
    // Both continue identically.
    const auto o=sim.step();
    id.add(o);copy.add(o);
    CHECK(std::fabs(id.report(sim.utc).model.tau_h-copy.report(sim.utc).model.tau_h)<1e-12);
    BuildingIdentifier other;
    CHECK(!other.load(0x9999u,blob.data(),used));      // other pump/identity
    CHECK(other.report(sim.utc).state==CalibrationState::NoData);
    std::vector<uint8_t> corrupt(blob.begin(),blob.begin()+used);
    corrupt[used/2]^=0x40;
    CHECK(!other.load(0x1234u,corrupt.data(),used));   // CRC
    CHECK(!other.load(0x1234u,blob.data(),used-1));    // size/format
    CHECK(!other.save(1,blob.data(),16,used));         // too small buffer
}

void test_trend_ring() {
    Sim sim;
    BuildingIdentifier id;
    for(int i=0;i<150;++i) id.add(sim.step());
    std::vector<TrendPoint> t(BuildingIdentifier::trend_slots);
    const size_t n=id.trend(t.data(),t.size());
    CHECK(n==BuildingIdentifier::trend_slots);
    for(size_t i=1;i<n;++i) CHECK(t[i].utc==t[i-1].utc+900);
    CHECK(t[n-1].utc==sim.utc);
}
} // namespace

int main() {
    test_no_data_is_honest();
    test_comfort_band();
    test_recovers_truth_and_calibrates();
    test_no_heating_never_calibrates();
    test_meter_scope_rule();
    test_rejects_replay_gap_and_outlier();
    test_wrong_prior_converges();
    test_degrades_when_building_changes();
    test_moderate_change_degrades();
    test_stale_validation();
    test_persistence();
    test_trend_ring();
    if(failures) {std::printf("%d identification check(s) failed\n",failures);return 1;}
    std::puts("cottage_identification_tests: all passed");
    return 0;
}
