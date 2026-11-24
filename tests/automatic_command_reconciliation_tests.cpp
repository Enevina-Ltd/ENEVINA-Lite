#include "cottage/automatic_command_reconciliation.hpp"
#include "cottage/mitsubishi_control_policy.hpp"
#include <cassert>
#include <iostream>
using namespace enevina;
using namespace enevina::cottage;
int main(){
    AutomaticCommandReconciliation pending{};
    assert(pending.allow(1,false,PumpRoomMode::Unknown));
    pending.unresolved(1,PumpRoomMode::Heat,true,23);
    assert(pending.awaiting(1));
    // The Codex regression: old setpoint remains readable after a SET timeout.
    for(int i=0;i<100;++i)assert(!pending.allow(1,true,PumpRoomMode::Heat,21));
    assert(!pending.allow(1,false,PumpRoomMode::Heat,23));
    assert(!pending.allow(1,true,PumpRoomMode::Unknown,23));
    assert(!pending.allow(1,true,PumpRoomMode::Other,23));
    assert(!pending.allow(1,true,PumpRoomMode::Off,23));
    assert(!pending.allow(1,true,PumpRoomMode::Heat,NAN));
    assert(pending.allow(1,true,PumpRoomMode::Heat,23));
    assert(!pending.awaiting(1));
    assert(pending.allow(1,false,PumpRoomMode::Unknown));
    pending.unresolved(2,PumpRoomMode::Off,false);
    assert(!pending.allow(2,true,PumpRoomMode::Heat,23));
    assert(pending.allow(2,true,PumpRoomMode::Off,NAN));
    pending.unresolved(3,PumpRoomMode::Off,true,22);
    assert(!pending.allow(3,true,PumpRoomMode::Off,21));
    assert(pending.allow(3,true,PumpRoomMode::Off,22)); // separate CN105 start
    pending.unresolved(4,PumpRoomMode::Heat,true,24);
    assert(pending.allow(5,false,PumpRoomMode::Unknown)); // explicit new arm
    assert(!pending.awaiting(5));
    pending.unresolved(5,PumpRoomMode::Heat,true,25);
    assert(!pending.allow(5,true,PumpRoomMode::Heat,24));
    assert(pending.allow(5,true,PumpRoomMode::Heat,25));
    pending.unresolved(6,PumpRoomMode::Unknown,false);
    assert(!pending.allow(6,true,PumpRoomMode::Unknown));
    pending.clear();
    assert(pending.allow(6,false,PumpRoomMode::Unknown));
    assert(std::string(control_gate_name(ControlBlock::AwaitingReadback))=="AWAITING_READBACK");
    std::cout<<"PASS uncertain automatic dispatch, matching fresh readback, mode preservation and explicit reauthorization\n";
}
