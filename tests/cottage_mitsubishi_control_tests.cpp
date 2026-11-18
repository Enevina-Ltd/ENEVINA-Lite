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
int main() {
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
}
