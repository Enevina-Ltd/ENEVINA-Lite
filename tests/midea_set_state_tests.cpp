#include "pump/midea_v3_protocol.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace enevina::midea;
Bytes hex(const char* t) {
    Bytes v(std::strlen(t)/2);
    assert(decode_hex(t,v.data(),v.size()));
    return v;
}
int main() {
    State s{};
    s.power=true;s.mode=4;s.setpoint_c=22;s.fan=102;s.swing=1;
    ControlPatch patch{};
    patch.changed=ControlTemperature;patch.setpoint_c=22.0;
    Bytes frame;
    assert(set_state_command(s,patch,1,frame));
    assert(frame==hex("aa24ac00000000000002400386667f7f003100000000000000000000002800000000011e89"));
    assert(frame[9]==2 && frame[10]==0x40 && frame.size()==37);
    s.power=true;s.mode=4;s.setpoint_c=22;s.fan=40;s.swing=1;
    s.eco=true;s.sleep=true;s.purifier=true;
    patch.setpoint_c=23.5;
    assert(set_state_command(s,patch,2,frame));
    assert(frame==hex("aa24ac00000000000002400397287f7f003100a00100000000000000002800000000027cb6"));
    State after=s;after.setpoint_c=23.5;
    assert(control_patch_observed(after,patch));
    after.setpoint_c=22;
    assert(!control_patch_observed(after,patch));
    s={};s.power=false;s.mode=2;s.setpoint_c=16;s.fan=102;
    patch.changed=ControlPower;patch.power=false;
    assert(set_state_command(s,patch,3,frame));
    assert(frame==hex("aa24ac00000000000002400240667f7f00300000000000000000000004280000000003f6f3"));
    s.power=true;s.mode=4;s.setpoint_c=31;
    patch.changed=ControlTemperature;patch.setpoint_c=31;
    assert(set_state_command(s,patch,4,frame));
    assert(frame==hex("aa24ac00000000000002400380667f7f00300000000000000000000013280000000004b8e0"));
    s.power=true;s.mode=4;s.setpoint_c=21;s.fan=102;
    patch={};patch.changed=ControlPower|ControlMode|ControlFan|ControlSwing;
    patch.power=false;patch.mode=2;patch.fan=40;patch.swing=3;
    assert(set_state_command(s,patch,5,frame));
    assert(frame[11]==0x02 && frame[12]>>5==2 &&
           frame[13]==40 && frame[17]==0x33);
    State verified=s;
    verified.power=false;verified.mode=2;verified.fan=40;verified.swing=3;
    assert(control_patch_observed(verified,patch));
    verified.fan=102;
    assert(!control_patch_observed(verified,patch));
    patch.changed=0;
    assert(!set_state_command(s,patch,6,frame));
    patch.changed=ControlTemperature;patch.setpoint_c=22.3;
    assert(!set_state_command(s,patch,6,frame));
    patch.setpoint_c=100;
    assert(!set_state_command(s,patch,6,frame));
    patch.setpoint_c=std::nan("");
    assert(!set_state_command(s,patch,6,frame));
    patch.changed=ControlMode;patch.mode=7;
    assert(!set_state_command(s,patch,6,frame));
    patch.changed=ControlFan;patch.fan=200;
    assert(!set_state_command(s,patch,6,frame));
    patch.changed=0x80;
    assert(!set_state_command(s,patch,6,frame));
    std::cout<<"Midea V2/V3 shared AC 0x40 full-state command vectors PASS\n";
}
