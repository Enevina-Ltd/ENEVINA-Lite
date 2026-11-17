#include "cottage/mitsubishi_control_policy.hpp"
#include <cassert>
#include <limits>
using namespace enevina::cottage;
static ControlSnapshot base() {
    ControlSnapshot s;
    s.armed=true;s.hold_mode=true;
    s.temperature_fresh=s.market_fresh=true;
    s.cn105_link_fresh=s.bridge_get_ok=s.pump_origin_verified=true;
    s.current_heat=false;s.current_setpoint_c=21;
    s.now_ms=10000000;
    s.proposal.action=BootstrapAction::HeatSetpoint;
    s.proposal.heat_setpoint_c=18.5;
    return s;
}
int main() {
    auto s=base();
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
    s.armed=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::Disabled);
    s=base();s.hold_mode=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::NotInHoldMode);
    s=base();s.temperature_fresh=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::InputsUnverified);
    s=base();s.market_fresh=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::InputsUnverified);
    s=base();s.cn105_link_fresh=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::InputsUnverified);
    s=base();s.bridge_get_ok=false;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::InputsUnverified);
    s=base();s.pump_origin_verified=false;
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
    s.has_confirmed_mode_change=true;s.last_mode_change_ms=s.now_ms-3599999;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::ModeDwell);
    s.last_mode_change_ms=s.now_ms-3600000;
    assert(choose_mitsubishi_control(s).step==ControlStep::SetOff);
    s=base();s.has_last_command=true;s.last_command_ms=s.now_ms-59999;
    assert(choose_mitsubishi_control(s).gate==ControlBlock::RateLimited);
    s.last_command_ms=s.now_ms-60000;
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
    s=base();s.current_setpoint_c=std::numeric_limits<double>::quiet_NaN();
    assert(choose_mitsubishi_control(s).step==ControlStep::Setpoint);
}
