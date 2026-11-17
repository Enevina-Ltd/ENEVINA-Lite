#include "cottage/ruuvi_v5.hpp"
#include "cottage/air_humidity.hpp"
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
    cfg.mode=CottageMode::Hold;cfg.hold_target_c=12;
    auto hold=evaluate_cottage_mode(cfg,wet,true,true,0,0);
    assert(hold.has_target && near(hold.target_c,12) &&
           hold.direct_setpoint_below_mitsubishi_min&&
           hold.advisory_heat_needed && !hold.physical_writes_allowed);
    hold=evaluate_cottage_mode(cfg,wet,false,true,0,0);
    assert(!hold.has_target && !hold.advisory_heat_needed);
    cfg.hold_target_c=18;
    hold=evaluate_cottage_mode(cfg,wet,true,true,0,0);
    assert(!hold.direct_setpoint_below_mitsubishi_min);
    assert(!hold.physical_writes_allowed); // still shadow even when possible

    std::puts("PASS official RAWv2, bonding, duplicate/stale, dewpoint, history, alarms and all 3 shadow modes");
}
