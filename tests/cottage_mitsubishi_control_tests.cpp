#include "cottage/mitsubishi_control_policy.hpp"
#include <cassert>
#include <cstring>
#include <limits>
using namespace enevina::cottage;
static ControlSnapshot base() {
    ControlSnapshot s;
    s.armed=true;s.heating_mode=true;
    s.temperature_fresh=s.market_fresh=true;
    s.cn105_link_fresh=s.bridge_get_ok=true;
    s.current_heat=false;s.current_setpoint_c=21;
    s.proposal.action=BootstrapAction::HeatSetpoint;
    s.proposal.heat_setpoint_c=18.5;
    s.ms_since_mode_change=mitsubishi_min_mode_dwell_ms; // history old enough
    return s;
}
static void shared_gate_respects_driver_limits_and_dwell() {
    auto s=base();
    const enevina::PumpHeatingLimits lan{20,28,1,true,true};
    auto decision=choose_pump_control(s,lan,HeatTransition::Combined);
    assert(decision.gate==ControlBlock::UnsupportedCommand);
    s.proposal.heat_setpoint_c=22;
    s.ms_since_mode_change=0;
    assert(choose_pump_control(s,lan,HeatTransition::Combined).gate==ControlBlock::ModeDwell);
    s.ms_since_mode_change=mitsubishi_min_mode_dwell_ms;
    decision=choose_pump_control(s,lan,HeatTransition::Combined);
    assert(decision.step==ControlStep::SetHeat && decision.target_c==22);
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
    s.current_heat=true;
    assert(choose_pump_control(s,lan,HeatTransition::Combined).step==ControlStep::Setpoint);
    s.proposal.action=BootstrapAction::Off;
    const enevina::PumpHeatingLimits no_off{20,28,1,true,false};
    assert(choose_pump_control(s,no_off,HeatTransition::Combined).gate==ControlBlock::UnsupportedCommand);
}

int main() {
    shared_gate_respects_driver_limits_and_dwell();
    {
        // Regression for a small room that reaches its maintenance target
        // before the default 30-minute HEAT request is over.
        constexpr uint32_t minute=60000;
        BootstrapInput low{};low.indoor_fresh=true;low.desired_room_c=12;
        low.low_temperature_cycle=true;
        auto s=base();s.current_setpoint_c=16;s.current_heat=true;
        s.ms_since_mode_change=8*minute;
        low.indoor_c=11.9;s.proposal=bootstrap_price_decision(low);
        assert(choose_mitsubishi_control(s).step==ControlStep::None);
        low.indoor_c=12.1;s.proposal=bootstrap_price_decision(low);
        assert(choose_mitsubishi_control(s).step==ControlStep::SetOff);
        assert(mode_change_allowed(ControlStep::SetOff,8*minute,
            mitsubishi_min_mode_dwell_ms,s.proposal,true));
        // Other adapters retain 30 min; ordinary scoped price-only OFF retains 15 min.
        assert(choose_pump_control(s,mitsubishi_heating_limits,HeatTransition::Combined).gate==ControlBlock::ModeDwell);
        auto ordinary=s;ordinary.proposal.reason=BootstrapReason::ExpensiveWindow;
        assert(choose_mitsubishi_control(ordinary).gate==ControlBlock::ModeDwell);
        // Permission, fresh measurements, connection and valid readback
        // remain mandatory for a shortened HEAT request.
        auto bad=s;bad.armed=false;
        assert(choose_mitsubishi_control(bad).gate==ControlBlock::Disabled);
        bad=s;bad.temperature_fresh=false;
        assert(choose_mitsubishi_control(bad).gate==ControlBlock::InputsUnverified);
        bad=s;bad.bridge_get_ok=false;
        assert(choose_mitsubishi_control(bad).gate==ControlBlock::InputsUnverified);
        bad=s;bad.proposal.requested_room_c=16;
        assert(choose_mitsubishi_control(bad).gate==ControlBlock::ModeDwell);
        // Scoped 15-min restart dwell remains even on a cold/cheap signal.
        s.current_heat=false;low.indoor_c=11.0;s.proposal=bootstrap_price_decision(low);
        s.ms_since_mode_change=15*minute-1;
        assert(choose_mitsubishi_control(s).gate==ControlBlock::ModeDwell);
        s.ms_since_mode_change=15*minute;
        assert(choose_mitsubishi_control(s).step==ControlStep::SetHeat);
        assert(choose_pump_control(s,mitsubishi_heating_limits,HeatTransition::Combined).gate==ControlBlock::ModeDwell);
        auto high=s;high.proposal.heat_setpoint_c=17;high.current_setpoint_c=17;
        assert(choose_mitsubishi_control(high).gate==ControlBlock::ModeDwell);
        // Unchanged high-temperature normal thermostat does not bypass.
        low.low_temperature_cycle=false;low.indoor_c=12.7;
        s.proposal=bootstrap_price_decision(low);s.current_heat=true;
        s.ms_since_mode_change=8*minute;
        assert(choose_mitsubishi_control(s).gate==ControlBlock::ModeDwell);
    }

    {
        constexpr uint32_t minute=60u*1000u;
        auto start=base();
        BootstrapInput room{};
        room.indoor_fresh=true;
        room.indoor_c=7.2;
        room.desired_room_c=12;
        start.proposal=bootstrap_price_decision(room);
        assert(start.proposal.reason==BootstrapReason::ThermostatWithoutPrices);
        assert(start.proposal.heat_setpoint_c==16.0);
        start.current_setpoint_c=16.0;
        MitsubishiModeHistory bridge;
        bridge.observe_bridge(false,minute);
        start.ms_since_mode_change=bridge.since_change(minute);
        assert(choose_mitsubishi_control(start).step==ControlStep::SetHeat);

        bridge.command_dispatched(2*minute);
        // The first readback may still show OFF. Turning automation back on
        // cannot erase an attempted write or allow a duplicate HEAT.
        bridge.observe_bridge(false,3*minute);
        start.ms_since_mode_change=bridge.since_change(3*minute);
        assert(choose_mitsubishi_control(start).gate==ControlBlock::ModeDwell);
        bridge.observe_bridge(true,4*minute);
        bridge.observe_bridge(false,5*minute);
        start.ms_since_mode_change=bridge.since_change(6*minute);
        assert(choose_mitsubishi_control(start).gate==ControlBlock::ModeDwell);

        MitsubishiModeHistory running;
        running.observe_bridge(true,minute);
        auto stop=start;
        stop.current_heat=true;
        stop.proposal.action=BootstrapAction::Off;
        stop.ms_since_mode_change=running.since_change(minute);
        assert(choose_mitsubishi_control(stop).step==ControlStep::SetOff);
        running.command_dispatched(minute);
        stop.ms_since_mode_change=running.since_change(2*minute);
        assert(choose_mitsubishi_control(stop).gate==ControlBlock::ModeDwell);

        // A read gap or endpoint change is different from arming.
        MitsubishiModeHistory disconnected;
        disconnected.observe_bridge(false,minute);
        disconnected.observe_bridge(false,8*minute);
        start.ms_since_mode_change=disconnected.since_change(8*minute);
        assert(choose_mitsubishi_control(start).gate==ControlBlock::ModeDwell);
        disconnected.reset(9*minute);
        disconnected.observe_bridge(false,9*minute);
        start.ms_since_mode_change=disconnected.since_change(9*minute);
        assert(choose_mitsubishi_control(start).gate==ControlBlock::ModeDwell);
    }
    auto s=base();
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
    s.armed=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::Disabled);
    s=base();s.heating_mode=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::NotInHeatingMode);
    s=base();s.temperature_fresh=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::InputsUnverified);
    // Cold's explicit OFF works without a live sensor: no fake heat demand.
    s=base();s.temperature_fresh=true;s.current_heat=true;
    s.proposal.action=BootstrapAction::Off;
    assert(choose_mitsubishi_control(s).step==ControlStep::SetOff);
    s=base();s.market_fresh=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::InputsUnverified);
    s=base();s.cn105_link_fresh=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::InputsUnverified);
    s=base();s.bridge_get_ok=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::InputsUnverified);
    s=base();s.proposal.action=BootstrapAction::Unavailable;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::NoProposal);
    s=base();s.proposal.heat_setpoint_c=18.25;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::UnsupportedCommand);
    s=base();s.current_setpoint_c=18.5;
    assert(choose_mitsubishi_control(s).step==ControlStep::SetHeat);
    s=base();s.current_heat=true;s.current_setpoint_c=18.5;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::AlreadyApplied);
    s=base();s.proposal.action=BootstrapAction::Off;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::AlreadyApplied);
    s.current_heat=true;
    assert(choose_mitsubishi_control(s).step==ControlStep::SetOff);
    // Owner decision 18.11.2026: 30 min minimum between HEAT<->OFF changes.
    s=base();s.current_setpoint_c=18.5;           // would be SetHeat
    s.ms_since_mode_change=mitsubishi_min_mode_dwell_ms-1;
    auto blocked=choose_mitsubishi_control(s);
    assert(blocked.gate==ControlBlock::ModeDwell && blocked.step==ControlStep::None);
    assert(std::strcmp(control_gate_name(blocked.gate),"MODE_DWELL")==0);
    s.ms_since_mode_change=mitsubishi_min_mode_dwell_ms;
    assert(choose_mitsubishi_control(s).step==ControlStep::SetHeat);
    s=base();s.current_heat=true;s.proposal.action=BootstrapAction::Off;
    s.ms_since_mode_change=0;                     // e.g. just booted
    assert(choose_mitsubishi_control(s).gate==ControlBlock::ModeDwell);
    // A setpoint change is not a mode change and is never delayed.
    s=base();s.ms_since_mode_change=0;
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
    {
        MitsubishiModeHistory history{};
        const uint32_t first_observation=61u*60u*1000u;
        history.observe(false,first_observation,true);
        assert(history.since_change(first_observation)==0);
        history.observe(false,first_observation+30000,true);
        assert(history.since_change(first_observation+30000)==30000);
    }
    // MitsubishiModeHistory (Codex post-merge review of #28).
    {
        constexpr uint32_t m=60u*1000u;
        MitsubishiModeHistory h;                   // boot: unknown history
        assert(h.since_change(0)==0 && h.since_change(29*m)<mitsubishi_min_mode_dwell_ms);
        h.observe(true,1*m);                       // first sight: no restart
        assert(h.since_change(31*m)>=mitsubishi_min_mode_dwell_ms);
        h.observe(true,32*m);
        h.observe(false,33*m);                     // observed change restarts
        assert(h.since_change(34*m)==1*m);
        // P1 repro: last seen HEAT at 34 min, link lost, pump toggled OFF at
        // 45 and HEAT at 75 min unseen, GET back at 76 min with HEAT again.
        MitsubishiModeHistory g;
        g.reset(0);g.observe(true,1*m);g.observe(true,34*m);
        g.observe(true,76*m);                      // gap 42 min > 5 min
        ControlSnapshot c=base();
        c.current_heat=true;c.proposal.action=BootstrapAction::Off;
        c.ms_since_mode_change=g.since_change(77*m);
        assert(choose_mitsubishi_control(c).gate==ControlBlock::ModeDwell);
        c.ms_since_mode_change=g.since_change(106*m);
        assert(choose_mitsubishi_control(c).step==ControlStep::SetOff);
        // Short gaps between regular ticks do not restart the dwell.
        MitsubishiModeHistory r;
        r.observe(false,0);r.observe(false,4*m);r.observe(false,8*m);
        assert(r.since_change(40*m)==40*m);
        // A dispatched command restarts it; reset() (arming) does too.
        r.command_dispatched(41*m);
        assert(r.since_change(42*m)==1*m);
        r.reset(50*m);
        assert(r.since_change(50*m)==0);
        // uint32 millis() wrap-around stays correct.
        MitsubishiModeHistory w;
        w.reset(0xFFFFFF00u);
        assert(w.since_change(0x00000100u)==0x200u);
    }
    // Codex P1 repro (PR #28): target 12 C, room at 11.99/12.00 C, cheap and
    // expensive 15-min slots alternating. Without a dwell HEAT/OFF changed
    // every 15 min; now mode changes are >= 30 min apart.
    {
        const double cheapp[]={0.05,0.30,0.30,0.30,0.30,0.30,0.30,0.30};
        const double dearp[]={0.60,0.30,0.30,0.30,0.30,0.30,0.30,0.30};
        bool heat=false;
        uint32_t since=mitsubishi_min_mode_dwell_ms,last_change_slot=0;
        unsigned changes=0;
        for(unsigned slot=0;slot<16;++slot) {
            BootstrapInput in{};
            in.indoor_fresh=true;in.desired_room_c=12;
            in.indoor_c=slot%2?12.00:11.99;
            in.next_total_prices=slot%2?dearp:cheapp;in.price_count=8;
            in.prices_contiguous_and_fresh=true;
            ControlSnapshot c=base();
            c.current_heat=heat;c.current_setpoint_c=16;
            c.proposal=bootstrap_price_decision(in);
            c.ms_since_mode_change=since;
            const auto out=choose_mitsubishi_control(c);
            if(out.step==ControlStep::SetHeat || out.step==ControlStep::SetOff) {
                if(changes) assert(slot-last_change_slot>=2); // >= 30 min
                heat=out.step==ControlStep::SetHeat;
                ++changes;last_change_slot=slot;since=0;
            } else {
                since+=15u*60u*1000u;
            }
        }
        assert(changes>=1 && changes<=8);
    }
    s=base();s.current_setpoint_c=std::numeric_limits<double>::quiet_NaN();
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
    {
        MitsubishiCommandReadback r{};
        r.sent={ControlStep::Setpoint,21.5,100};
        CN105Telemetry obs{};
        obs.received=true;
        obs.mode=ObservedPumpMode::Off;
        obs.setpoint_c=21.5;
        obs.evidence={104,99,true};
        r.observe(obs,104);
        assert(r.confirmed.step==ControlStep::None); // buffered pre-write data
        obs.evidence={104,100,true};
        r.observe(obs,104);
        assert(r.confirmed.step==ControlStep::None); // second-level ambiguity
        obs.evidence={104,103,false};
        r.observe(obs,104);
        assert(r.confirmed.step==ControlStep::None); // network receipt only
        obs.evidence.source_time_verified=true;
        obs.setpoint_c=21;
        r.observe(obs,104);
        assert(r.confirmed.step==ControlStep::None);
        obs.setpoint_c=21.5;
        r.observe(obs,104);
        assert(r.confirmed.step==ControlStep::Setpoint && r.confirmed.utc==103);
        r.sent={ControlStep::SetHeat,21.5,110};
        obs.mode=ObservedPumpMode::Heat;
        obs.evidence={115,114,true};
        obs.setpoint_c=21;
        r.observe(obs,115);
        assert(r.confirmed.step==ControlStep::Setpoint);
        obs.setpoint_c=21.5;
        r.observe(obs,115);
        assert(r.confirmed.step==ControlStep::SetHeat && r.confirmed.utc==114);
        r.sent={ControlStep::SetOff,0,120};
        obs.mode=ObservedPumpMode::Other;
        obs.evidence={124,123,true};
        r.observe(obs,124);
        assert(r.confirmed.step==ControlStep::SetHeat); // cooling is not off
        obs.mode=ObservedPumpMode::Off;
        r.observe(obs,300);
        assert(r.confirmed.step==ControlStep::SetHeat); // late delivery
        r.observe(obs,124);
        assert(r.confirmed.step==ControlStep::SetOff && r.confirmed.utc==123);
    }
    // Real cottage field case, 2026-11-19: Ruuvi 11.585 C, user hold 12 C,
    // heat pump currently OFF and its setpoint 21 C. The hysteresis boundary
    // is 11.5 C. A HOLD creates NO physical writes; after crossing 11.5 C
    // the executable sequence is valid SETPOINT -> HEAT, with fresh CN105
    // HTTP GET verified at runtime and the owner's 30-min mode dwell.
    {
        BootstrapInput in{};
        in.indoor_fresh=true;
        in.desired_room_c=12.0;
        in.indoor_c=11.585;
        const double neutral_prices[]={0.15,0.15,0.15,0.15,0.15,0.15,0.15,0.15};
        in.next_total_prices=neutral_prices;
        in.price_count=8;
        in.prices_contiguous_and_fresh=true;
        auto proposal=bootstrap_price_decision(in);
        assert(proposal.action==BootstrapAction::Hold);
        ControlSnapshot cs=base();
        cs.current_heat=false;
        cs.current_setpoint_c=21;
        cs.proposal=proposal;
        assert(choose_mitsubishi_control(cs).gate==ControlBlock::NoProposal);
        in.indoor_c=11.49;
        proposal=bootstrap_price_decision(in);
        assert(proposal.action==BootstrapAction::HeatSetpoint);
        assert(proposal.heat_setpoint_c==16.0);
        cs.proposal=proposal;
        assert(choose_mitsubishi_control(cs).step==ControlStep::Setpoint);
        cs.current_setpoint_c=16.0;
        assert(choose_mitsubishi_control(cs).step==ControlStep::SetHeat);
        cs.ms_since_mode_change=0;
        assert(choose_mitsubishi_control(cs).gate==ControlBlock::ModeDwell);
        cs.temperature_fresh=false;
        assert(choose_mitsubishi_control(cs).gate==ControlBlock::InputsUnverified);
    }
}
