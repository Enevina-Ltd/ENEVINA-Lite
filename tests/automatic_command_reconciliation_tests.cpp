#include "cottage/automatic_command_reconciliation.hpp"
#include "cottage/mitsubishi_control_policy.hpp"
#include "cottage/learning_observation.hpp"
#include <cassert>
#include <iostream>
using namespace enevina;
using namespace enevina::cottage;
int main(){
    AutomaticCommandReconciliation pending{};
    assert(!pending.observe(1,false,PumpRoomMode::Unknown));
    assert(!pending.awaiting(1));
    pending.unresolved(1,PumpRoomMode::Heat,true,23);
    assert(pending.awaiting(1));
    // The Codex regression: old setpoint remains readable after a SET timeout.
    for(int i=0;i<100;++i)assert(!pending.observe(1,true,PumpRoomMode::Heat,21));
    assert(!pending.observe(1,false,PumpRoomMode::Heat,23));
    assert(!pending.observe(1,true,PumpRoomMode::Unknown,23));
    assert(!pending.observe(1,true,PumpRoomMode::Other,23));
    assert(!pending.observe(1,true,PumpRoomMode::Off,23));
    assert(!pending.observe(1,true,PumpRoomMode::Heat,NAN));
    assert(pending.observe(1,true,PumpRoomMode::Heat,23));
    assert(!pending.awaiting(1));
    assert(!pending.observe(1,false,PumpRoomMode::Unknown));
    assert(!pending.awaiting(1));
    pending.unresolved(2,PumpRoomMode::Off,false);
    assert(!pending.observe(2,true,PumpRoomMode::Heat,23));
    assert(pending.observe(2,true,PumpRoomMode::Off,NAN));
    pending.unresolved(3,PumpRoomMode::Off,true,22);
    assert(!pending.observe(3,true,PumpRoomMode::Off,21));
    assert(pending.observe(3,true,PumpRoomMode::Off,22)); // separate CN105 start
    pending.unresolved(4,PumpRoomMode::Heat,true,24);
    assert(!pending.observe(5,false,PumpRoomMode::Unknown)); // not a matching poll
    assert(pending.awaiting(4)); // new authorization may dispatch without clearing old receipt
    assert(!pending.awaiting(5));
    pending.unresolved(5,PumpRoomMode::Heat,true,25);
    assert(!pending.observe(5,true,PumpRoomMode::Heat,24));
    assert(pending.observe(5,true,PumpRoomMode::Heat,25));
    pending.unresolved(6,PumpRoomMode::Unknown,false);
    assert(!pending.observe(6,true,PumpRoomMode::Unknown));
    pending.clear();
    assert(!pending.observe(6,false,PumpRoomMode::Unknown));
    assert(!pending.awaiting(6));
    assert(std::string(control_gate_name(ControlBlock::AwaitingReadback))=="AWAITING_READBACK");
    // Cached matches are never observations; just checking the gate must
    // not consume pending state. A later independent poll resolves it.
    pending.unresolved(7,PumpRoomMode::Heat,true,20);
    for(int i=0;i<100;++i)assert(pending.awaiting(7));
    assert(!pending.observe(8,true,PumpRoomMode::Heat,20));
    assert(pending.awaiting(7));
    assert(pending.observe(7,true,PumpRoomMode::Heat,20));
    assert(!pending.observe(7,true,PumpRoomMode::Heat,20));
    PumpRoomState start{};
    start.source=PumpStateSource::DirectLan;
    start.protocol=PumpRoomProtocol::GreeEcb;
    start.mode=PumpRoomMode::Heat;
    start.setpoint_c=20;
    start.endpoint_generation=1;start.link_generation=2;
    start.configuration_generation=3;start.request_generation=4;
    start.write_generation=5;
    auto end=start;end.request_minutes=15;
    assert(quiet_common_learning_request(start,end));
    for(int fault=0;fault<12;++fault) {
        auto a=start,b=end;
        switch(fault) {
            case 0:a.write_in_progress=true;break;
            case 1:b.write_in_progress=true;break;
            case 2:++b.write_generation;break;
            case 3:a.command_unconfirmed=true;break;
            case 4:b.command_unconfirmed=true;break;
            case 5:++b.request_generation;break;
            case 6:++b.configuration_generation;break;
            case 7:++b.link_generation;break;
            case 8:++b.endpoint_generation;break;
            case 9:b.setpoint_c=21;break;
            case 10:b.source=PumpStateSource::None;break;
            case 11:b.request_minutes=13;break;
        }
        assert(!quiet_common_learning_request(a,b));
    }
    start.mode=end.mode=PumpRoomMode::Off;
    start.setpoint_c=end.setpoint_c=NAN;
    assert(quiet_common_learning_request(start,end));
    end.mode=PumpRoomMode::Other;assert(!quiet_common_learning_request(start,end));
    std::cout<<"PASS uncertain automatic dispatch, matching fresh readback, mode preservation and explicit reauthorization\n";
}
