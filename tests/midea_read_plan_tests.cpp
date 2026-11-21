#include "pump/midea_read_plan.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace enevina::midea;
int main() {
    ReadIdentity a{1234,0x0a000001,1,5,17,3};
    ReadPlan plan;
    assert(plan.bind(a));
    assert(!plan.bind(a));
    assert(plan.next_probe()==0);
    plan.finish_probe(0,false);
    assert(plan.next_probe()==1); // no one timeout is permanent unsupported
    plan.finish_probe(1,true);
    assert(plan.next_probe()==2);
    plan.finish_probe(2,true);
    assert(plan.next_probe()==0);
    plan.finish_probe(0,false);
    assert(plan.next_probe()==-1); // bounded after two attempts
    auto b=a;b.configuration_generation++;
    assert(plan.bind(b) && plan.next_probe()==0); // Same-device re-enrollment retries metadata.
    b.link_generation++;
    assert(plan.bind(b) && plan.next_probe()==0);
    b.endpoint_generation++;
    assert(plan.bind(b) && plan.next_probe()==0);
    b.device_id++;
    assert(plan.bind(b));
    b.address++;
    assert(plan.bind(b));
    b.transport=2;
    assert(plan.bind(b));
    assert(!same_read_identity(a,b));
    // Deadlines remain correct across the ESP millisecond counter wrapping.
    const uint32_t start=UINT32_MAX-20;
    assert(exchange_time_left(start,50,UINT32_MAX-10)==40);
    assert(exchange_time_left(start,50,10)==19);
    assert(exchange_time_left(start,50,29)==0);
    assert(exchange_time_left(start,50,30)==0);
    assert(command_window_open(1700000001,1700000015,100,101,5000));
    assert(!command_window_open(1700000015,1700000015,100,101,5000));
    assert(!command_window_open(1700000001,1700000015,100,5100,5000));
    assert(!command_window_open(0,1700000015,100,101,5000));
    std::cout<<"Midea negotiation identity and bounded deadline plan PASS\n";
}
