#include "cottage/baseload_learning.hpp"
#include "cottage/site_settings.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace enevina;
using namespace enevina::cottage;

struct Fixture {
    BaseloadLearner learner{};
    PumpRoomState request{};
    ShellyTelemetry meter{};
    int64_t now=1795600000;
    uint32_t id=17;
    Fixture(){
        request.source=PumpStateSource::Gateway;
        request.protocol=PumpRoomProtocol::Cn105EspHome;
        request.mode=PumpRoomMode::Off;
        request.mode_minutes=request.request_minutes=5;
        request.request_generation=1;
        meter.received=true;meter.source_identity=id;
        meter.total_wh=1234;
    }
    BaseloadEstimate step(double watts,bool relay=true,bool defrost=false,int seconds=60) {
        now+=seconds;
        request.evidence.received_utc=now;
        meter.captured_utc=now-3;meter.watts=watts;
        meter.total_wh+=watts*seconds/3600.0;
        return learner.observe(id,now,request,meter,relay,defrost);
    }
};

int main() {
    Fixture f;
    BaseloadEstimate b{};
    const double values[]={166,170,174,167,169,172,168};
    for(unsigned i=0;i<7;++i){
        b=f.step(values[i]);
        assert(b.ready==(i==6));
    }
    assert(b.samples==7 && b.watts==169 && b.spread_w==8);
    const auto original=b;
    // No replay, or a repeated meter sample, can advance a window.
    b=f.learner.observe(f.id,f.now,f.request,f.meter,true,false);
    assert(b.revision==original.revision && b.observed_utc==original.observed_utc);
    f.request.mode=PumpRoomMode::Heat;
    b=f.step(1650);
    assert(b.ready && b.watts==169);
    assert(!baseload_current(b,f.id,f.now+7201));
    assert(!baseload_current(b,99,f.now));
    double kwh=0;
    assert(incremental_slot_energy(original,b,f.id,f.now,f.now+900,1000,1292.25,true,kwh));
    assert(std::fabs(kwh-0.250)<1e-9);
    assert(incremental_slot_energy(original,b,f.id,f.now,f.now+900,1000,1042.25,false,kwh) && kwh<1e-9);
    assert(!incremental_slot_energy(original,b,f.id,f.now,f.now+900,1000,1200,false,kwh));
    assert(!incremental_slot_energy(original,b,f.id,f.now,f.now+900,1000,999,true,kwh));
    assert(!incremental_slot_energy(original,b,f.id,f.now,f.now+600,1000,1200,true,kwh));
    // Real change withdraws estimate immediately; do not blend a changing
    // background load into a fake heat-pump energy model.
    f.request.mode=PumpRoomMode::Off;++f.request.request_generation;
    b=f.step(300);assert(!b.ready);
    for(int i=0;i<6;++i)b=f.step(299+i%2);
    assert(b.ready && b.watts>=299 && b.watts<=300 && b.revision!=original.revision);
    assert(!incremental_slot_energy(original,b,f.id,f.now,f.now+900,1000,1200,true,kwh));
    b=f.step(0,false);assert(!b.ready);
    Fixture g;
    for(int i=0;i<20;++i) {
        g.request.mode=PumpRoomMode::Heat;
        assert(!g.step(1600).ready);
    }
    g.request.mode=PumpRoomMode::Off;
    g.request.mode_minutes=g.request.request_minutes=1;
    for(int i=0;i<8;++i)assert(!g.step(170).ready);
    g.request.mode_minutes=g.request.request_minutes=5;
    for(int i=0;i<7;++i)assert(!g.step(i%2?170:800).ready);
    for(int i=0;i<6;++i){b=g.step(170);assert(!b.ready || b.samples==7);}
    b=g.step(170);assert(b.ready);
    g.meter.total_wh=0;
    b=g.step(170);assert(!b.ready);
    Fixture replay;
    for(int i=0;i<6;++i)assert(!replay.step(170).ready);
    for(int i=0;i<20;++i)assert(!replay.learner.observe(replay.id,replay.now,replay.request,replay.meter,true,false).ready);
    replay.request.command_unconfirmed=true;assert(!replay.step(170).ready);
    replay.request.command_unconfirmed=false;
    for(int i=0;i<6;++i)assert(!replay.step(170).ready);
    replay.request.link_generation++;assert(!replay.step(170).ready);
    // Real polling cadence may be much faster than one minute. The old
    // fixed seven-entry ring never reached a five-minute span at 15 s.
    Fixture fast;
    for(int i=0;i<20;++i)assert(!fast.step(167,true,false,15).ready);
    for(int i=0;i<8;++i)b=fast.step(167,true,false,15);
    assert(b.ready && b.samples==7 && b.watts==167);
    b=fast.step(800,true,false,15);assert(!b.ready);
    Fixture interrupted;
    for(int i=0;i<16;++i)assert(!interrupted.step(167,true,false,15).ready);
    // An intervening high load must not be hidden by decimation.
    assert(!interrupted.step(800,true,false,15).ready);
    for(int i=0;i<20;++i)assert(!interrupted.step(167,true,false,15).ready);
    for(int i=0;i<8;++i)b=interrupted.step(167,true,false,15);
    assert(b.ready && b.watts==167);
    Fixture changes;
    for(int i=0;i<7;++i)b=changes.step(170);
    assert(b.ready);
    changes.id=18;changes.meter.source_identity=18;
    assert(!changes.step(170).ready);
    // Scope consent never inferred merely from a high OFF load.
    SiteSettings cfg{};
    std::strcpy(cfg.shelly_url,"http://192.168.0.4");
    assert(observed_shelly_meter_identity(cfg)!=0);
    assert(confirmed_shared_meter_identity(cfg)==0 && confirmed_shelly_meter_identity(cfg)==0);
    const auto unknown=observed_shelly_meter_identity(cfg);
    cfg.shelly_scope=ShellyScope::WholeHouse;
    assert(confirmed_shared_meter_identity(cfg)!=0 && confirmed_shared_meter_identity(cfg)!=unknown);
    assert(confirmed_shelly_meter_identity(cfg)==0);
    cfg.shelly_scope=ShellyScope::HeatPump;
    assert(confirmed_shared_meter_identity(cfg)==0 && confirmed_shelly_meter_identity(cfg)!=0);
    std::cout<<"PASS robust OFF baseline, changes, stale/replay, scope and counter-based increments\n";
}
