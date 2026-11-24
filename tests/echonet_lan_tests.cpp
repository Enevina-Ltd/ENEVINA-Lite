#include "pump/echonet_lan.hpp"
#include <cassert>
#include <iostream>
using namespace enevina;
using namespace enevina::echonet;
int main(){
    const Object obj{1,0x30,1};
    // Discovery keeps exact object identity, not just the node IP.
    CandidateList devices;
    assert(devices.add({1,1}) && devices.add({1,2}) && devices.add({1,1}));
    assert(devices.count==2 && !devices.add({0,1}) && !devices.add({1,0}) && !devices.add({1,128}));
    for(unsigned i=3;i<=CandidateList::capacity;++i)assert(devices.add({1,Byte(i)}));
    assert(!devices.add({2,1}) && devices.truncated && devices.count==CandidateList::capacity);
    assert(response_peer(1,1,3610,49,50,true));
    assert(!response_peer(1,2,3610,49,50,true));
    assert(!response_peer(1,1,3611,49,50,true));
    assert(!response_peer(1,1,3610,50,50,true));
    assert(!response_peer(1,1,3610,49,50,false));
    assert(response_peer(0,2,3610,49,50,true)); // caller also checks same subnet
    assert(!response_peer(0,0,3610,49,50,true));
    Identity a{};a.size=17;a.bytes[0]=0xfe;a.bytes[3]=8;a.bytes[4]=1;
    auto b=a;assert(a.valid() && a==b);
    const Property node{0x83,a.bytes.data(),a.size};
    assert(decode_identity(&node)==a);
    assert(!decode_identity(nullptr));
    const Property absent{0x83,nullptr,17};assert(!decode_identity(&absent));

    b.bytes[16]=2;assert(!(a==b));
    b=a;b.bytes[0]=0;assert(!b.valid());
    b=a;b.bytes[4]=0;assert(!b.valid());
    b=a;b.size=16;assert(!b.valid());
    b={};b.size=9;b.bytes[0]=0xff;b.bytes[1]=1;assert(b.valid());
    for(unsigned i=1;i<9;++i)b.bytes[i]=0xff;
    assert(!b.valid());
    Capabilities cap;
    for(Byte e:std::array<Byte,3>{0x80,0xb0,0xb3})cap.readable.codes[e-0x80]=cap.writable.codes[e-0x80]=true;
    assert(controllable(cap));
    auto missing=cap;missing.writable.codes[0xb0-0x80]=false;
    assert(!controllable(missing) && !same_caps(cap,missing) && same_caps(cap,cap));
    ApprovedModes approval{{16,25,true},{20,32,true}};
    assert(approval.valid() && approval.heating().accepts(24) && !approval.heating().accepts(24.5));
    assert(!ApprovedModes{}.valid());
    State current{};current.power=true;current.mode=Mode::Heat;current.setpoint_c=21;
    Desired heat{true,Mode::Heat,23},cool{true,Mode::Cool,27},off{false,{},{}},target{{},{},24};
    assert(approved_command(1,obj,cap,approval,current,heat));
    assert(approved_command(1,obj,cap,approval,current,cool));
    assert(approved_command(1,obj,cap,approval,current,off));
    assert(approved_command(1,obj,cap,approval,current,target));
    assert(!approved_command(1,obj,cap,approval,current,Desired{true,Mode::Heat,27}));
    assert(!approved_command(1,obj,cap,approval,current,Desired{true,Mode::Cool,16}));
    assert(!approved_command(1,obj,cap,approval,current,Desired{true,Mode::Dry,23}));
    assert(!approved_command(1,obj,cap,approval,current,Desired{false,Mode::Cool,23}));
    assert(!approved_command(1,obj,cap,approval,current,Desired{{},Mode::Cool,23}));
    assert(!approved_command(1,obj,cap,approval,current,Desired{true,{},{}}));
    assert(!approved_command(1,obj,missing,approval,current,heat));
    auto heat_only=approval;heat_only.cool.enabled=false;
    assert(!approved_command(1,obj,cap,heat_only,current,cool));
    auto cool_only=approval;cool_only.heat.enabled=false;
    assert(cool_only.valid() && !cool_only.heating().heating && !approved_command(1,obj,cap,cool_only,current,heat));
    assert(approved_command(1,obj,cap,cool_only,current,cool));
    auto after=current;after.setpoint_c=24;
    assert(applied(current,target,after) && !same_request(current,after));
    after.mode=Mode::Cool;assert(!applied(current,target,after));
    after=current;after.power=false;assert(!applied(current,target,after) && applied(current,off,after));
    assert(!approved_command(1,obj,cap,approval,after,target));
    assert(mode_change(current,cool) && mode_change(current,off) && !mode_change(current,target));
    assert(room_mode(current)==PumpRoomMode::Heat && room_mode(after)==PumpRoomMode::Off);
    after=current;after.mode=Mode::Cool;
    assert(room_mode(after)==PumpRoomMode::Other);
    assert(approved_command(1,obj,cap,approval,after,Desired{{},{},31}));
    assert(!approved_command(1,obj,cap,approval,after,Desired{{},{},16}));
    current.fault=true;assert(!approved_command(1,obj,cap,approval,current,heat));
    const Byte p=0x30,m=0x43,t=0x80;
    const Property power{0x80,&p,1},mode{0xb0,&m,1};
    auto operation=operation_state(&power,&mode);assert(operation && operation->power && operation->mode==Mode::Heat);
    Frame partial{};partial.count=1;partial.properties[0]={0xbb,&t,1};
    optional_state(*operation,partial);assert(!operation->indoor_c && !operation->setpoint_c);
    std::cout<<"PASS ECHONET LAN peer/session, exact identity, discovery, mode-specific approval and readback policies\n";
}
