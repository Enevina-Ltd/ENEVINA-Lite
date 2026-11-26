#include "cottage/ruuvi_v5.hpp"
#include "cottage/air_humidity.hpp"
#include "cottage/price_bootstrap.hpp"
#include "cottage/telemetry.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstring>
using namespace enevina::cottage;
static bool near(double a,double b,double tolerance=0.025) {
    return std::fabs(a-b)<=tolerance;
}
static std::vector<uint8_t> raw(const char* hex) {
    std::vector<uint8_t> a;
    while(hex[0]&&hex[1]) {
        unsigned byte=0;
        assert(std::sscanf(hex,"%02x",&byte)==1);
        a.push_back(static_cast<uint8_t>(byte));
        hex+=2;
    }
    return a;
}
int main() {
    // Official RAWv2/DF5 vectors:
    // https://docs.ruuvi.com/communication/bluetooth-advertisements/data-format-5-rawv2
    // bytes 99 04 are manufacturer ID (0x0499 LE), then 24 data bytes.
    auto valid=raw("99040512FC5394C37C0004FFFC040CAC364200CDCBB8334C884F");
    assert(valid.size()==26);
    RuuviV5 a{};
    assert(decode_ruuvi_v5(valid.data(),valid.size(),-47,a));
    assert(near(a.temperature_c,24.3,1e-5));
    assert(near(a.relative_humidity_pct,53.49,1e-5));
    assert(near(a.pressure_pa,100044,1e-5));
    assert(near(a.battery_v,2.977,1e-5));
    assert(a.tx_power_dbm==4 && a.sequence==205 &&
           a.movement_count==66 && a.rssi_dbm==-47);
    std::array<uint8_t,6> official_mac{};
    assert(parse_ruuvi_mac("CB:B8:33:4C:88:4F",official_mac));
    assert(official_mac==a.mac);
    auto invalid=raw("9904058000FFFFFFFF800080008000FFFFFFFFFFFFFFFFFFFFFF");
    assert(invalid.size()==26);
    assert(!decode_ruuvi_v5(invalid.data(),invalid.size(),-47,a));
    assert(!decode_ruuvi_v5(valid.data(),valid.size()-1,-47,a));
    auto bad=valid;bad[0]=0x01;
    assert(!decode_ruuvi_v5(bad.data(),bad.size(),-47,a));
    bad=valid;bad[2]=0xc5;
    assert(!decode_ruuvi_v5(bad.data(),bad.size(),-47,a));
    bad=valid;bad[5]=0xff;bad[6]=0xff;
    assert(!decode_ruuvi_v5(bad.data(),bad.size(),-47,a));
    // Official maximum vector is representable but not a plausible room.
    auto maxv=raw("9904057FFFFFFEFFFE7FFF7FFF7FFFFFDEFEFFFECBB8334C884F");
    assert(!decode_ruuvi_v5(maxv.data(),maxv.size(),-50,a));
    assert(decode_ruuvi_v5(valid.data(),valid.size(),-47,a));
    RuuviBinding gate;
    assert(gate.accept(a,1000)==RuuviAccept::Unbound);
    gate.bind(official_mac);
    // Codex P2: older hourly_v1 has no MAC in the record. Restoration
    // requires a durable matching NVS identity, not compiler fallback.
    assert(verified_history_origin(true,official_mac,gate));
    assert(!verified_history_origin(false,official_mac,gate));
    auto recompiled_mac=official_mac;
    recompiled_mac[0]^=0x02;
    assert(!verified_history_origin(true,recompiled_mac,gate));
    RuuviBinding unbound_history{};
    assert(!verified_history_origin(true,official_mac,unbound_history));
    assert(gate.accept(a,1000)==RuuviAccept::Accepted);
    assert(gate.accept(a,1200)==RuuviAccept::Duplicate);
    assert(gate.valid(500000));
    assert(!gate.valid(601001));
    RuuviV5 other=a;
    other.mac[0]^=0x11;other.sequence++;
    assert(gate.accept(other,1800)==RuuviAccept::DifferentSensor);
    RuuviV5 next=a;next.sequence=206;
    assert(gate.accept(next,1800)==RuuviAccept::Accepted);
    next.sequence=205;
    assert(gate.accept(next,2000)==RuuviAccept::OldSequence);
    next.sequence=207;next.temperature_c=35;
    assert(gate.accept(next,2100)==RuuviAccept::Implausible);
    next.temperature_c=24.5;
    assert(gate.accept(next,2100)==RuuviAccept::Accepted);
    // Codex P2: an RH=0 sample must not advance packet freshness.
    RuuviBinding moisture_gate;
    moisture_gate.bind(a.mac);
    assert(moisture_gate.accept(a,1000)==RuuviAccept::Accepted);
    for(unsigned seq=1;seq<=5;++seq) {
        RuuviV5 zero_rh=a;zero_rh.sequence=static_cast<uint16_t>(a.sequence+seq);
        zero_rh.relative_humidity_pct=0;
        assert(moisture_gate.accept(zero_rh,1000+seq*60000ULL)==RuuviAccept::Invalid);
    }
    assert(!moisture_gate.valid(1000+600001ULL));
    assert(moisture_gate.age_ms(1000+600001ULL)==600001ULL);
    // Codex P2: a stale bound sensor may reboot its sequence on battery
    // replacement. Never reset on one old/replayed frame; accept only
    // two advancing restart frames after the previous value has expired.
    RuuviBinding restart_gate;
    restart_gate.bind(a.mac);
    RuuviV5 before_restart=a;before_restart.sequence=10000;
    assert(restart_gate.accept(before_restart,1000)==RuuviAccept::Accepted);
    RuuviV5 restarted=before_restart;restarted.sequence=0;
    assert(restart_gate.accept(restarted,400000)==RuuviAccept::OldSequence);
    assert(restart_gate.valid(400000));
    assert(restart_gate.accept(restarted,700001)==RuuviAccept::OldSequence);
    assert(!restart_gate.valid(700001));
    restarted.sequence=1;
    assert(restart_gate.accept(restarted,701001)==RuuviAccept::Accepted);
    assert(restart_gate.valid(701001));
    assert(restart_gate.accept(restarted,701500)==RuuviAccept::Duplicate);
    restarted.sequence=0;
    assert(restart_gate.accept(restarted,702000)==RuuviAccept::OldSequence);
    gate.unbind();
    assert(!gate.valid(2100));
    assert(!parse_ruuvi_mac("FF:FF:FF:FF:FF:FF",official_mac));
    assert(!parse_ruuvi_mac("not a real MAC",official_mac));

    auto air=calculate_air_moisture(20,50);
    assert(air.valid);
    assert(near(air.dew_point_c,9.26,0.06));
    assert(near(air.vapour_pressure_pa,1166,4));
    assert(near(air.absolute_humidity_gm3,8.62,0.10));
    assert(!calculate_air_moisture(20,-1).valid);
    assert(!calculate_air_moisture(20,0).valid);
    assert(!calculate_air_moisture(-60,60).valid);
    assert(calculate_air_moisture(-20,75).valid);
    auto outdoor=calculate_air_moisture(0,60);
    auto vent=estimate_ventilation(air,outdoor);
    assert(vent.available && vent.possible_drying &&
           vent.inside_minus_outside_gm3>4);
    assert(!estimate_ventilation(air,AirMoisture{}).available);

    AirHistory hist;
    assert(hist.add(air,1700000000));
    assert(!hist.add(air,1700000200)); // same UTC hour
    assert(hist.add(air,1700004000));
    assert(hist.count()==2);
    auto dump=hist.dump();
    AirHistory restored;
    assert(restored.restore(dump));
    assert(restored.count()==hist.count());
    assert(restored.newest(0).hour_utc==hist.newest(0).hour_utc);
    // Codex P2: an unsynchronised reboot MUST NOT overwrite NVS-restored
    // UTC history with boot-relative hours, even before NTP arrives.
    AirHistory persisted_history;
    for(int i=0;i<48;i++)
        assert(persisted_history.add(air,1700000000LL+i*3600LL));
    AirHistory recovered_history;
    assert(recovered_history.restore(persisted_history.dump()));
    assert(recovered_history.count()==48);
    assert(!recovered_history.add_offline(air,3600000));
    assert(recovered_history.count()==48);
    assert(recovered_history.add(air,1700000000LL+48*3600LL));
    assert(recovered_history.count()==49);
    assert(recovered_history.newest(48).hour_utc==1700000000LL/3600);
    dump.checksum++;
    assert(!restored.restore(dump));
    assert(restored.count()==2);
    AirHistory offline;
    assert(offline.add_offline(air,0));
    assert(offline.newest(0).hour_utc&0x80000000u);
    assert(offline.add_offline(air,3600000));
    assert(offline.count()==2);
    assert(!restored.restore(offline.dump())); // no trusted UTC on restart
    assert(offline.add(air,1700000000)); // synchronization: new civil series
    assert(offline.count()==1);
    assert(!(offline.newest(0).hour_utc&0x80000000u));

    AdvisoryLimits limits{};
    limits.monitor_after_ms=6ULL*3600000;
    limits.review_after_ms=24ULL*3600000;
    MoistureMonitor never_seen(limits);
    never_seen.tick(1'000,true);
    assert(!never_seen.alert(MoistureAlertKind::SensorMissing).active);
    never_seen.tick(1'000+600000,true);
    assert(!never_seen.alert(MoistureAlertKind::SensorMissing).active);
    never_seen.tick(1'000+600001,true);
    assert(never_seen.alert(MoistureAlertKind::SensorMissing).active);
    never_seen.tick(1'000+600002,false);
    assert(!never_seen.alert(MoistureAlertKind::SensorMissing).active);
    MoistureMonitor monitor(limits);
    RuuviV5 humid=a;humid.temperature_c=12;
    humid.relative_humidity_pct=83;humid.battery_v=2.9;
    monitor.observe(humid,1000);
    assert(!monitor.alert(MoistureAlertKind::HighRh80).active);
    // Continuous high RH, at least one genuine packet every 5 minutes.
    for(uint64_t hour=1;hour<=25*12;hour++)
        monitor.observe(humid,1000+hour*300000);
    assert(monitor.alert(MoistureAlertKind::HighRh70).active);
    assert(monitor.alert(MoistureAlertKind::HighRh80).active);
    monitor.acknowledge(MoistureAlertKind::HighRh80);
    assert(monitor.alert(MoistureAlertKind::HighRh80).acknowledged);
    humid.relative_humidity_pct=70; // hysteresis: still held over 75? no
    monitor.observe(humid,1000+301*300000);
    assert(!monitor.alert(MoistureAlertKind::HighRh80).active);
    monitor.tick(1000+301*300000+600001,true);
    assert(monitor.alert(MoistureAlertKind::SensorMissing).active);
    assert(!monitor.alert(MoistureAlertKind::HighRh70).active);
    humid.relative_humidity_pct=65;humid.battery_v=2.3;
    humid.temperature_c=-1;
    monitor.observe(humid,1000+310*300000);
    assert(monitor.alert(MoistureAlertKind::LowBattery).active);
    assert(monitor.alert(MoistureAlertKind::Freezing).active);
    assert(!monitor.alert(MoistureAlertKind::SensorMissing).active);

    CottageModeConfig cfg{};
    auto cold=evaluate_cottage_mode(cfg,air,true,false,0,24ULL*3600000);
    assert(cold.mode==CottageMode::Cold && !cold.has_target &&
           !cold.advisory_heat_needed && !cold.physical_writes_allowed);
    cfg.mode=CottageMode::Drying;
    auto dry=evaluate_cottage_mode(cfg,air,true,true,8,0);
    {
        const auto moist=calculate_air_moisture(12,72);
        const auto advice=evaluate_cottage_mode(cfg,moist,true,true,16,7ULL*3600000);
        assert(advice.has_target && advice.target_c>12 && advice.target_c<20);
        assert(advice.drying.valid && near(advice.drying.estimated_rh_at_target_pct,65,0.001));
        const auto same_vapour=calculate_air_moisture(20,relative_humidity_at_temperature(moist,20));
        const auto exchange=estimate_ventilation(moist,same_vapour);
        assert(exchange.available && !exchange.possible_drying && near(exchange.drying_equivalent_gm3,0));
        assert(!assess_drying_heat(moist,20,0).valid);
        BootstrapInput input{};input.heating={18,28,1,true,true};
        input.desired_room_c=13;input.indoor_fresh=true;input.indoor_c=12;
        input.below_native_minimum_cycle=true;
        auto start=bootstrap_price_decision(input);
        assert(start.action==BootstrapAction::HeatSetpoint && start.heat_setpoint_c==18);
        assert(start.below_native_minimum_cycle && !start.low_temperature_cycle);
        input.indoor_c=13;
        assert(bootstrap_price_decision(input).action==BootstrapAction::Off);
        input.heating.off=false;
        assert(bootstrap_price_decision(input).action==BootstrapAction::Unavailable);
    }

    assert(dry.has_target && near(dry.target_c,12,1e-8) &&
           !dry.advisory_heat_needed &&
           dry.advisory==ModeAdvisory::MonitoringHumidity);
    auto missing_outside=evaluate_cottage_mode(cfg,air,true,false,0,24ULL*3600000);
    assert(!missing_outside.has_target &&
           missing_outside.advisory==ModeAdvisory::NoOutdoorTemperature);
    auto wet=calculate_air_moisture(10,85);
    dry=evaluate_cottage_mode(cfg,wet,true,true,8,7ULL*3600000);
    assert(dry.has_target&&dry.advisory_heat_needed &&
           dry.direct_setpoint_below_mitsubishi_min && !dry.physical_writes_allowed);
    // Codex P2: monitor exposure remains above clear threshold 65% even
    // while current RH dips to 69%. Do not thrash drying advice at 70%.
    auto hysteresis_air=calculate_air_moisture(10,69);
    auto held=evaluate_cottage_mode(cfg,hysteresis_air,true,true,8,7ULL*3600000);
    assert(held.advisory_heat_needed);
    assert(held.advisory==ModeAdvisory::UnsupportedBelow16);
    // Codex P2 (PR #28): once the RH-duration condition holds, drying no
    // longer blocks heat; the bootstrap's +-0.5 C deadband then holds the
    // current mode instead of toggling on a 0.02 C wobble.
    assert(drying_blocks_heat(dry)==false && drying_blocks_heat(held)==false);
    {
        auto near_target=calculate_air_moisture(11.51,75);
        auto plan=evaluate_cottage_mode(cfg,near_target,true,true,8,7ULL*3600000);
        assert(!plan.advisory_heat_needed && !drying_blocks_heat(plan));
        BootstrapInput in{};in.heating={16,26,0.5,true,true};
        in.indoor_fresh=true;in.desired_room_c=plan.target_c;
        const double seq[]={11.49,11.51,11.49,11.51};
        BootstrapAction last=BootstrapAction::Unavailable;
        unsigned toggles=0;bool heating=false;
        for(double t:seq) {
            in.indoor_c=t;
            const auto d=bootstrap_price_decision(in);
            bool next=heating;
            if(d.action==BootstrapAction::HeatSetpoint) next=true;
            else if(d.action==BootstrapAction::Off) next=false;
            if(last!=BootstrapAction::Unavailable && next!=heating) ++toggles;
            heating=next;last=d.action;
        }
        assert(toggles==0 && heating);
    }
    auto clear_air=calculate_air_moisture(10,64);
    auto cleared=evaluate_cottage_mode(cfg,clear_air,true,true,8,0);
    assert(!cleared.advisory_heat_needed);
    assert(cleared.advisory==ModeAdvisory::MonitoringHumidity);
    cfg.mode=CottageMode::Hold;cfg.hold_target_c=12;
    auto hold=evaluate_cottage_mode(cfg,wet,true,true,0,0);
    assert(hold.has_target && near(hold.target_c,12) &&
           hold.direct_setpoint_below_mitsubishi_min&&
           hold.advisory_heat_needed && !hold.physical_writes_allowed);
    hold=evaluate_cottage_mode(cfg,wet,false,true,0,0);
    assert(hold.has_target && near(hold.target_c,12) && !hold.advisory_heat_needed);
    assert(hold.advisory==ModeAdvisory::SensorUnavailable);
    auto unbound_hold=evaluate_cottage_mode(cfg,AirMoisture{},false,false,0,0);
    assert(unbound_hold.has_target && near(unbound_hold.target_c,12));
    assert(!unbound_hold.advisory_heat_needed && !unbound_hold.physical_writes_allowed);
    // Separate room-temperature selection feeds the price controller. A
    // missing Ruuvi cannot erase the configured target or force 'unavailable'.
    constexpr int64_t now=1800000000;
    enevina::PumpRoomState verified_pump{};
    verified_pump.source=enevina::PumpStateSource::DirectLan;
    verified_pump.evidence.received_utc=now-5;
    verified_pump.indoor_c=10.0;
    const auto measured=room_temperature_sample(
        {RoomTemperatureSource::Pump,0},{},verified_pump,now);
    assert(valid_temperature(measured,now));
    BootstrapInput pump_input{};pump_input.heating={16,26,0.5,true,true};
    pump_input.indoor_fresh=valid_temperature(measured,now);
    pump_input.indoor_c=measured.indoor_c;
    pump_input.desired_room_c=unbound_hold.target_c;
    assert(bootstrap_price_decision(pump_input).action==BootstrapAction::HeatSetpoint);
    pump_input.indoor_fresh=false;
    assert(bootstrap_price_decision(pump_input).action==BootstrapAction::Unavailable);
    cfg.hold_target_c=18;
    hold=evaluate_cottage_mode(cfg,wet,true,true,0,0);
    assert(!hold.direct_setpoint_below_mitsubishi_min);
    assert(!hold.physical_writes_allowed); // advisory evaluator is still pure

    // Living mode owns a separate room target; switching modes must not
    // overwrite the existing maintenance target.
    cfg.hold_target_c=12;
    cfg.mode=CottageMode::Occupied;
    cfg.occupied_target_c=21.0;
    auto lived=evaluate_cottage_mode(cfg,wet,true,true,0,0);
    assert(lived.mode==CottageMode::Occupied &&
           lived.has_target && near(lived.target_c,21.0) &&
           lived.advisory_heat_needed && !lived.direct_setpoint_below_mitsubishi_min);
    assert(cfg.hold_target_c==12.0);
    cfg.occupied_target_c=26.0;
    lived=evaluate_cottage_mode(cfg,wet,true,false,0,0);
    assert(lived.has_target && near(lived.target_c,26.0));
    cfg.occupied_target_c=15.5;
    lived=evaluate_cottage_mode(cfg,wet,true,true,0,0);
    assert(!lived.has_target);
    cfg.occupied_target_c=21.0;
    lived=evaluate_cottage_mode(cfg,wet,false,true,0,0);
    assert(lived.has_target && near(lived.target_c,21.0) && !lived.advisory_heat_needed);
    assert(lived.advisory==ModeAdvisory::SensorUnavailable);
    auto pump_only=evaluate_cottage_mode(cfg,AirMoisture{},false,false,0,0);
    assert(pump_only.has_target && near(pump_only.target_c,21.0));
    BootstrapInput living_input{};living_input.heating={16,26,0.5,true,true};
    living_input.indoor_fresh=true;
    living_input.indoor_c=19.0;
    living_input.desired_room_c=pump_only.target_c;
    assert(bootstrap_price_decision(living_input).action==BootstrapAction::HeatSetpoint);
    // Drying remains humidity-gated: the same missing Ruuvi must NOT enable it.
    cfg.mode=CottageMode::Drying;
    auto no_rh=evaluate_cottage_mode(cfg,AirMoisture{},false,true,8,24ULL*3600000);
    assert(!no_rh.has_target && drying_blocks_heat(no_rh));
    cfg.mode=CottageMode::Occupied;
    cfg.mode=CottageMode::Hold;
    hold=evaluate_cottage_mode(cfg,wet,true,true,0,0);
    assert(hold.has_target && near(hold.target_c,12));

    std::puts("PASS official RAWv2, bonding, duplicate/stale, dewpoint, history, alarms and all 4 cottage modes");
}
