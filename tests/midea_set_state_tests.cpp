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
    s.power=true;s.mode=4;s.setpoint_c=22;s.fan=102;s.swing=12;s.freeze_protection_known=true;
    ControlPatch patch{};
    patch.changed=ControlTemperature;patch.setpoint_c=22.0;
    Bytes frame;
    assert(set_state_command(s,patch,1,frame));
    assert(frame==hex("aa24ac00000000000002400386667f7f003c0000000000000000000000280000000001d6c6"));
    assert(frame[9]==2 && frame[10]==0x40 && frame.size()==37);
    s.power=true;s.mode=4;s.setpoint_c=22;s.fan=40;s.swing=12;
    s.eco=true;s.sleep=true;s.purifier=true;
    patch.setpoint_c=23.5;
    assert(set_state_command(s,patch,2,frame));
    assert(frame==hex("aa24ac00000000000002400397287f7f003c00a0010000000000000000280000000002b473"));
    State after=s;after.setpoint_c=23.5;
    assert(control_patch_observed(after,patch));
    assert(preserved_state_observed(s,after,patch));
    after.independent_aux_heat=true;
    assert(!preserved_state_observed(s,after,patch)); // fail if safety flag changes
    after.independent_aux_heat=false;
    after.freeze_protection=true;
    assert(!preserved_state_observed(s,after,patch));
    after.freeze_protection=false;
    after.setpoint_c=22;
    assert(!control_patch_observed(after,patch));
    s={};s.power=false;s.mode=2;s.setpoint_c=16;s.fan=102;s.freeze_protection_known=true;
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
    patch.changed=ControlTemperature;patch.setpoint_c=40.5;
    assert(!set_state_command(s,patch,7,frame));
    patch.setpoint_c=22;
    s.freeze_protection_known=false;
    s.freeze_protection=false; // msmart-ng fallback for a short C0
    assert(set_state_command(s,patch,8,frame)); // ordinary temperature only
    assert(frame[31]==0); // optional 8 C heat preset inactive at 22 C
    // Short C0 cannot justify arbitrary full-state controls.
    patch.changed=ControlFan;patch.fan=80;
    assert(!set_state_command(s,patch,8,frame));
    patch.changed=ControlMode;patch.mode=2;
    assert(!set_state_command(s,patch,8,frame));
    patch.changed=ControlPower;patch.power=false;
    assert(!set_state_command(s,patch,8,frame));
    patch.changed=ControlSwing;patch.swing=12;
    assert(!set_state_command(s,patch,8,frame));
    patch.changed=ControlTemperature;patch.setpoint_c=22;
    State freeze_preset=s;freeze_preset.setpoint_c=8;
    assert(!set_state_command(freeze_preset,patch,8,frame));
    freeze_preset.setpoint_c=12;
    assert(!set_state_command(freeze_preset,patch,8,frame));
    patch.setpoint_c=12;
    assert(!set_state_command(s,patch,8,frame));
    patch.setpoint_c=22;
    // Contradictory unknown state must not be accepted.
    freeze_preset=s;freeze_preset.freeze_protection=true;
    assert(!set_state_command(freeze_preset,patch,8,frame));
    State short_after=s;
    short_after.setpoint_c=patch.setpoint_c;
    assert(preserved_state_observed(s,short_after,patch));
    short_after.aux_heat=!short_after.aux_heat;
    assert(!preserved_state_observed(s,short_after,patch));
    s.freeze_protection_known=true;
    s.freeze_protection=true;
    s.independent_aux_heat=true;
    s.swing=15;
    assert(set_state_command(s,patch,9,frame));
    assert(frame[17]==0x3f);
    assert(frame[31]==0x80); // freeze
    assert(frame[32]==0x08); // independent aux heat
    s.swing=12;
    assert(set_state_command(s,patch,10,frame));
    assert(frame[17]==0x3c);
    s.swing=2;
    assert(!set_state_command(s,patch,11,frame));
    std::cout<<"Midea V2/V3 shared AC 0x40 full-state command vectors PASS\n";
}
