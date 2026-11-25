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
int main(){
    BootstrapInput in{};in.indoor_fresh=true;in.desired_room_c=12;
    in.low_temperature_cycle=true;in.maintenance_storage=true;in.maintenance_ceiling_c=15.8;
    in.indoor_c=11.5;assert(bootstrap_price_decision(in).action==BootstrapAction::HeatSetpoint);
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
    assert(mpc_comfort_band(12,band,{0,13},false,true));
    assert(band.upper_c<=13);
    assert(mpc_comfort_band(12,band,{},false,false) && band.upper_c==13);
    Fixture f;f.begin();assert(f.r.candidate==2);
    f.complete();assert(f.r.completed==1 && f.r.candidate==0);
    f.complete();assert(f.r.completed==2 && f.r.candidate==1);
    f.complete();assert(f.r.completed==3 && f.r.candidate==2);
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
    assert(regime.r.candidate==2);
    // Small changes within the same heat regime retain completed evidence.
    Fixture jitter;jitter.begin();jitter.complete();
    jitter.s.base_w=178;++jitter.s.base_revision;jitter.tick(true,12,1500);
    assert(jitter.r.completed==1 && jitter.r.compared[2]==1);
    // An immediate material change without a missing sample also resets.
    jitter.s.base_w=1000;++jitter.s.base_revision;jitter.tick(true,12,1500);
    assert(jitter.r.completed==0 && jitter.r.compared[2]==0);
    // A costly forecast is not permission to spend energy exploring short
    // cycles. Keep the qualified wide candidate until cheaper evidence.
    Fixture cost;cost.s.price_priority=true;cost.begin();
    cost.complete();assert(cost.r.completed==1 && cost.r.candidate==2);
    cost.complete();assert(cost.r.completed==2 && cost.r.candidate==2);
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
