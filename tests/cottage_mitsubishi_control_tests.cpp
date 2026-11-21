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
