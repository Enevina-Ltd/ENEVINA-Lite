#include "cottage/mitsubishi_control_policy.hpp"
#include <cassert>
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
    // No invented 60-minute dwell or 15-minute retry ban:
    // only real device capabilities and valid input constrain commands.
    s=base();
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
    s=base();s.current_setpoint_c=std::numeric_limits<double>::quiet_NaN();
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
}
