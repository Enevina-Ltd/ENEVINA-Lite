#include "cottage/maintenance_cycle.hpp"
#include "cottage/price_bootstrap.hpp"
#include "cottage/building_identification.hpp"
#include "cottage/mpc_arbiter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace enevina::cottage;
struct Fixture {
    MaintenanceCycleLearner learner;
    MaintenanceCycleSample s;
    MaintenanceCycleReport r;
    Fixture() {
        s.utc=1795600000;s.room_c=11.5;s.outdoor_c=-2;
        s.total_wh=100000;s.base_w=173.3;s.price=0.20;s.price_valid=true;
        s.meter_id=3;s.base_revision=1;s.endpoint=2;s.link=1;
        s.request_generation=4;s.valid=true;
        r=learner.observe(true,3,1,1,12,s);
    }
    void tick(bool heat,double room,double net_w=0) {
        s.utc+=60;s.room_c=room;
        if(s.heat!=heat){s.heat=heat;++s.request_generation;++s.sent;++s.confirmed;s.ack_utc=s.utc-30;}
        s.total_wh+=(s.base_w+net_w)/60.0;
        r=learner.observe(true,3,1,1,12,s);
    }
    void begin(){tick(true,11.5,1500);}
    void complete() {
        const unsigned c=r.candidate;
        const int heat_min=c==2?30:c==1?16:8;
        const int total_min=c==2?180:c==1?80:30;
        const double max_c=maintenance_cycle_ceiling(12,0,c);
        for(int j=1;j<heat_min;++j)tick(true,11.5+(max_c-11.5)*j/(heat_min-1),1500);
        for(int j=heat_min;j<total_min;++j)
            tick(false,max_c-(max_c-11.5)*(j-heat_min)/(total_min-heat_min-1));
        begin();
    }
};
static void natural_phase_regression() {
    HeatPhaseObserver observer;
    HeatPhaseSample sample{};
    sample.heat=true;sample.generation=1;sample.utc=100;sample.hz=38;
    sample.pump_room=14;sample.room=12;sample.setpoint=16;sample.outdoor=-12;sample.watts=850;
    assert(observer.observe(sample)==HeatPhase::Starting);
    sample.utc+=30;sample.room=12.3;
    assert(observer.observe(sample)==HeatPhase::Heating);
    sample.utc+=30;sample.pump_room=16;sample.room=12.35;sample.hz=0;
    assert(observer.observe(sample)==HeatPhase::Unknown);
    assert(observer.observe(sample)==HeatPhase::Unknown); // duplicate cannot qualify
    sample.clear_of_defrost=true;
    sample.utc+=30;
    assert(observer.observe(sample)==HeatPhase::Unknown);
    sample.utc+=30;
    assert(observer.observe(sample)==HeatPhase::ThermostatStop);
    sample.defrost=true;sample.utc+=30;sample.hz=55;
    assert(observer.observe(sample)==HeatPhase::Defrost);
    sample.defrost=false;sample.utc+=30;sample.hz=0;
    assert(observer.observe(sample)==HeatPhase::Recovery);
    sample.hz=38;sample.utc+=30;
    assert(observer.observe(sample)==HeatPhase::Recovery);
    sample.utc+=30;sample.room=12.65;
    assert(observer.observe(sample)==HeatPhase::Recovery);
    sample.utc+=30;sample.room=12.7;sample.hz=18;sample.watts=300;
    assert(observer.observe(sample)==HeatPhase::Heating);
    sample.utc+=30;sample.room=12.75;
    assert(observer.observe(sample)==HeatPhase::Modulating);
    sample.outdoor=-20;sample.utc+=30;
    assert(observer.observe(sample)==HeatPhase::Heating);
    sample.fault=true;sample.utc+=30;
    assert(observer.observe(sample)==HeatPhase::Fault);
}

static void trial_control_regression() {
    CycleTrial t{};
    t.strategy=CycleStrategy::Thermostat;t.lower=11.5;t.upper=16;t.started_utc=100;
    t.expires_utc=200;t.expires_ms=1000;t.site=3;t.config=5;t.authorization=7;
    assert(t.current(100,999,3,5,7) && !t.current(100,999,3,5,8));
    assert(!t.current(200,999,3,5,7) && !t.current(199,1000,3,5,7));
    BootstrapDecision d{};
    d.action=BootstrapAction::Off;d.requested_room_c=12;d.low_temperature_cycle=true;
    const auto apply=[&](double room,HeatPhase phase){return cycle_trial_proposal(d,t,room,true,16,phase);};
    assert(apply(12.0,HeatPhase::Heating).action==BootstrapAction::Hold);
    assert(apply(12.0,HeatPhase::Unknown).action==BootstrapAction::Hold);
    assert(apply(16.0,HeatPhase::Heating).action==BootstrapAction::Off);
    assert(apply(16.0,HeatPhase::Defrost).action==BootstrapAction::Hold);
    assert(apply(12.0,HeatPhase::ThermostatStop).reason==BootstrapReason::ThermostatCycleEnd);
    assert(apply(12.0,HeatPhase::Modulating).action==BootstrapAction::Hold);
    assert(apply(11.4,HeatPhase::Modulating).heat_setpoint_c==16);
    t.strategy=CycleStrategy::Modulation;
    assert(apply(12.0,HeatPhase::Modulating).reason==BootstrapReason::ModulationCycleEnd);
    assert(apply(11.9,HeatPhase::Modulating).action==BootstrapAction::Hold);

    Fixture f;
    f.s.strategy=CycleStrategy::Thermostat;f.s.trial_revision=1;
    f.s.trial_lower=11;f.s.trial_upper=16.5;
    f.tick(false,11.5);
    f.s.phase=HeatPhase::Starting;
    f.begin();
    f.s.phase=HeatPhase::ThermostatStop;
    f.tick(true,12.5,500);
    f.s.phase_stop_command=true;
    for(int i=0;i<17;++i)f.tick(false,12.5-i/16.0);
    f.begin();
    const auto& result=f.r.trials[1];
    assert(result.cycle.valid && result.phase_end_seen && result.startup_seen);
    assert(result.cycle.cost_valid && result.total_cost>result.cycle.cost);
    assert(result.total_kwh>result.cycle.energy_kwh && result.loss_degree_hours>0);
    assert(std::abs(result.total_kwh-result.cycle.energy_kwh-result.baseload_kwh)<1e-9);
    assert(f.r.completed==0 && f.r.trial_counts[1]==1);
    MaintenanceCycleReport evidence{};
    evidence.trials[0].lower=11;evidence.trials[0].upper=16.5;
    evidence.trial_matched={2,2,1,2};
    evidence.trial_cost_per_hour={0.12,0.08,0.06,0.10};
    assert(select_observed_cycle_strategy(evidence,11,16.5,true)==CycleStrategy::Thermostat);
    assert(select_observed_cycle_strategy(evidence,11,16.5,false)==CycleStrategy::Forecast);
    assert(select_observed_cycle_strategy(evidence,11,16,true)==CycleStrategy::Forecast);
}
int main(){
    natural_phase_regression();
    trial_control_regression();
    BootstrapInput in{};in.heating={16,26,0.5,true,true};in.indoor_fresh=true;in.desired_room_c=12;
    in.low_temperature_cycle=true;in.maintenance_storage=true;in.maintenance_ceiling_c=15.8;
    // Without price data, never charge the building to an unexplained 15.8 C.
    in.indoor_c=11.5;assert(bootstrap_price_decision(in).action==BootstrapAction::HeatSetpoint);
    in.indoor_c=12;assert(low_temperature_stop(bootstrap_price_decision(in)));
    const double neutral[8]={.2,.2,.2,.2,.2,.2,.2,.2};
    in.next_total_prices=neutral;in.price_count=8;in.prices_contiguous_and_fresh=true;
    in.indoor_c=12.4;assert(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    in.indoor_c=15.79;assert(bootstrap_price_decision(in).action==BootstrapAction::Hold);
    in.indoor_c=15.8;assert(low_temperature_stop(bootstrap_price_decision(in)));
    in.comfort_upper_c=13;in.indoor_c=13;assert(low_temperature_stop(bootstrap_price_decision(in)));
    in.comfort_upper_c=0;in.indoor_c=12;in.maintenance_storage=false;
    assert(low_temperature_stop(bootstrap_price_decision(in)));
    in.low_temperature_cycle=false;in.maintenance_storage=true;in.indoor_c=13;
    assert(!bootstrap_price_decision(in).maintenance_storage);
    in.low_temperature_cycle=true;in.maintenance_ceiling_c=16.1;
    assert(bootstrap_price_decision(in).action==BootstrapAction::Unavailable);
    ComfortBand band{};assert(mpc_comfort_band(12,band,{},false,true));
    assert(std::fabs(band.upper_c-15.8)<1e-8 && std::fabs(band.lower_c-11.5)<1e-8);
    assert(mpc_comfort_band(12,band,{9,13},false,true));
    assert(band.lower_c==9);
    assert(mpc_comfort_band(12,band,{0,13},false,true));
    assert(band.upper_c<=13);
    assert(mpc_comfort_band(12,band,{},false,false) && band.upper_c==13);
    Fixture f;f.begin();assert(f.r.candidate==0);
    f.complete();assert(f.r.completed==1 && f.r.candidate==1);
    f.complete();assert(f.r.completed==2 && f.r.candidate==2);
    f.complete();assert(f.r.completed==3 && f.r.candidate==0);
    f.complete();f.complete();f.complete();
    assert(f.r.completed==6 && f.r.candidate==2);
    assert(f.r.compared[0]==2 && f.r.compared[1]==2 && f.r.compared[2]==2);
    assert(f.r.rate_kw[2]<f.r.rate_kw[1] && f.r.rate_kw[1]<f.r.rate_kw[0]);
    assert(f.r.last.cost_valid && f.r.last.energy_kwh>0);
    // Outside matched weather no comparison is represented as established.
    assert(f.learner.report(-10).compared[2]==0);
    // A changed background heat source invalidates ALL old comparisons,
    // not just the open cycle. This must work across a data gap as well.
    Fixture regime;regime.begin();regime.complete();regime.complete();regime.complete();
    assert(regime.r.completed==3);
    regime.s.valid=false;
    regime.learner.observe(true,3,1,1,12,regime.s);
    regime.s.valid=true;regime.s.base_w=1000;++regime.s.base_revision;
    regime.tick(true,12,1500);
    assert(regime.r.completed==0 && !regime.r.last.valid);
    for(auto n:regime.r.compared)assert(n==0);
    assert(regime.r.candidate==0);
    // Small changes within the same heat regime retain completed evidence.
    Fixture jitter;jitter.begin();jitter.complete();
    jitter.s.base_w=178;++jitter.s.base_revision;jitter.tick(true,12,1500);
    assert(jitter.r.completed==1 && jitter.r.compared[0]==1);
    // An immediate material change without a missing sample also resets.
    jitter.s.base_w=1000;++jitter.s.base_revision;jitter.tick(true,12,1500);
    assert(jitter.r.completed==0 && jitter.r.compared[0]==0);
    // A costly forecast is not permission to spend energy exploring short
    // cycles. Keep the ordinary target until wider storage has evidence.
    Fixture cost;cost.s.price_priority=true;cost.s.tariff_adjusted=true;cost.begin();
    cost.complete();assert(cost.r.tariff_completed==1 && cost.r.completed==0 && cost.r.candidate==0);
    cost.complete();assert(cost.r.tariff_completed==2 && cost.r.completed==0 && cost.r.candidate==0);
    assert(cost.r.last_tariff.valid && cost.r.last_tariff.energy_kwh>0);
    for(auto n:cost.r.compared)assert(n==0);
    // A tariff change during a cycle must not contaminate the ordinary
    // candidate comparison or discard the measured incremental energy.
    Fixture shifted;shifted.begin();shifted.s.tariff_adjusted=true;
    shifted.tick(true,11.6,1500);shifted.s.tariff_adjusted=false;
    shifted.complete();
    assert(shifted.r.tariff_completed==1 && shifted.r.completed==0);
    assert(shifted.r.last_tariff.start_room_c==11.5);
    shifted.s.base_w=800;++shifted.s.base_revision;shifted.tick(true,12,1500);
    assert(shifted.r.tariff_completed==0 && !shifted.r.last_tariff.valid);
    // Reboot and OTA restore only complete, CRC-checked records with a
    // stable identity and a newly observed compatible background load.
    MaintenanceCycleKey key{11,3,27,12,0,0};
    MaintenanceCycleImage image{};
    assert(f.learner.checkpoint_dirty() && f.learner.save(key,image));
    f.learner.checkpoint_saved();assert(!f.learner.checkpoint_dirty());
    auto sample=f.s;
    sample.utc+=60;sample.heat=false;sample.request_generation=0;
    sample.sent=sample.confirmed=sample.failed=0;
    MaintenanceCycleLearner reboot;
    assert(reboot.restore(key,42,73,sample,image));
    auto resumed=reboot.observe(true,3,42,73,12,sample);
    assert(resumed.restored && !resumed.checkpoint_dirty && resumed.completed==6);
    assert(resumed.site_generation==42 && resumed.config_generation==73);
    assert(resumed.candidate==2 && resumed.compared==f.r.compared);
    // The interrupted HEAT request was not restored.
    sample.utc+=60;
    resumed=reboot.observe(true,3,42,73,12,sample);
    assert(resumed.completed==6 && resumed.rejected==0);
    auto wrong=key;wrong.identity++;
    assert(!reboot.restore(wrong,42,73,sample,image));
    wrong=key;wrong.tariff++;
    assert(!reboot.restore(wrong,42,73,sample,image));
    wrong=key;wrong.lower=9;
    assert(!reboot.restore(wrong,42,73,sample,image));
    auto foreign_load=sample;foreign_load.base_w=800;
    assert(!reboot.restore(key,42,73,foreign_load,image));
    auto stale=sample;stale.valid=false;
    assert(!reboot.restore(key,42,73,stale,image));
    auto corrupt=image;corrupt.records[0][0].energy+=1;
    assert(!reboot.restore(key,42,73,sample,corrupt));
    // An internally CRC-consistent but impossible record also fails.
    corrupt=image;corrupt.records[0][0].flags=255;
    corrupt.crc=history_crc16(reinterpret_cast<const uint8_t*>(&corrupt),offsetof(MaintenanceCycleImage,crc));
    assert(!reboot.restore(key,42,73,sample,corrupt));
    auto future=sample;future.utc=image.last.end_utc-1;
    assert(!reboot.restore(key,42,73,future,image));
    auto aged=sample;aged.utc+=31*86400;
    assert(reboot.restore(key,42,73,aged,image));
    resumed=reboot.report(aged.outdoor_c);
    assert(resumed.candidate==0 && !resumed.last.valid);
    for(auto n:resumed.compared)assert(n==0);
    // Price-directed evidence survives reboot but is not a matched-cycle
    // comparison or a reason to resume a previous heating command.
    assert(cost.learner.save(key,image));
    auto tariff_sample=cost.s;tariff_sample.utc+=60;
    assert(reboot.restore(key,4,5,tariff_sample,image));
    resumed=reboot.report(tariff_sample.outdoor_c);
    assert(resumed.tariff_completed==2 && resumed.last_tariff.valid);
    assert(resumed.completed==0 && resumed.candidate==0);
    // A material base-load change dirties the checkpoint immediately.
    reboot.observe(true,3,4,5,12,tariff_sample);
    tariff_sample.base_w=900;tariff_sample.utc+=60;++tariff_sample.base_revision;
    resumed=reboot.observe(true,3,4,5,12,tariff_sample);
    assert(resumed.checkpoint_dirty && !resumed.restored && resumed.tariff_completed==0);
    assert(reboot.save(key,image));
    assert(image.tariff_completed==0 && image.last_tariff.flags==0);
    Fixture missed;missed.begin();
    missed.s.valid=false;missed.s.measurement_gap_only=true;
    missed.tick(true,11.55,1500);
    assert(missed.r.rejected==0);
    missed.s.valid=true;missed.s.measurement_gap_only=false;
    missed.tick(true,11.6,1500);missed.complete();
    assert(missed.r.completed==1 && missed.r.rejected==0);
    Fixture unverified;unverified.begin();
    unverified.s.valid=false;unverified.s.measurement_gap_only=true;
    ++unverified.s.request_generation;unverified.tick(true,11.6,1500);
    assert(unverified.r.rejected==1);
    Fixture missing_long;missing_long.begin();
    missing_long.s.valid=false;missing_long.s.measurement_gap_only=true;
    missing_long.s.utc+=180;missing_long.tick(true,11.6,1500);
    assert(missing_long.r.rejected==1);
    Fixture gaps;gaps.begin();gaps.s.utc+=180;gaps.tick(true,12,1500);
    assert(gaps.r.completed==0 && gaps.r.rejected==1);
    Fixture reset;reset.begin();++reset.s.base_revision;reset.tick(true,12,1500);
    assert(reset.r.completed==0 && reset.r.rejected==1);
    Fixture foreign;foreign.begin();++foreign.s.request_generation;
    foreign.tick(true,12,1500);assert(foreign.r.rejected==1);
    auto disabled=f.learner.observe(false,3,1,1,12,f.s);
    assert(!disabled.enabled && disabled.completed==0);
    std::cout<<"PASS storage ceiling, unchanged ordinary modes, full-cycle energy comparison, exploration, stale and foreign evidence rejection\n";
}
