#include "pump/midea_ac_protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace enevina::midea;
namespace {
int failures=0;
void check(bool ok,const char* text,int line) {
    if(!ok) { std::cerr<<"line "<<line<<": "<<text<<'\n'; ++failures; }
}
#define CHECK(x) check((x),#x,__LINE__)
Bytes hex(const char* text) {
    Bytes result;
    for(size_t i=0;text[i];i+=2) {
        char pair[]{text[i],text[i+1],0};
        result.push_back(static_cast<uint8_t>(std::strtoul(pair,nullptr,16)));
    }
    return result;
}
// Synthetic transport fixture framing is independent of the production codec.
// Payloads below describe only public protocol fields, never a physical device.
Bytes response(Bytes body,uint8_t type=3,uint8_t protocol=8) {
    uint8_t crc=0;
    for(uint8_t value:body) {
        crc^=value;
        for(unsigned bit=0;bit<8;++bit)crc=(crc&1)?(crc>>1)^0x8c:crc>>1;
    }
    body.push_back(crc);
    Bytes frame{0xaa,static_cast<uint8_t>(10+body.size()),0xac,0,0,0,0,0,protocol,type};
    frame.insert(frame.end(),body.begin(),body.end());
    uint8_t sum=0;
    for(size_t i=1;i<frame.size();++i)sum+=frame[i];
    frame.push_back(static_cast<uint8_t>(-sum));
    return frame;
}
void basic_families() {
    // msmart-ng short C0 capture already used by the V3 interoperability tests.
    const Bytes short_c0=hex("aa1eac00000000000003c0004b1e7f7f000000000069630000000000000d33");
    State s;
    CHECK(ingest_ac_response(short_c0.data(),short_c0.size(),s)==ResponseKind::BasicState);
    CHECK(s.has_basic && s.mode==2 && s.setpoint_c==27 && !s.power);
    CHECK(!s.freeze_protection_known);
    // A0 uses a different setpoint layout from C0 (27.5 C in byte 1).
    Bytes a0(23,0);a0[0]=0xa0;a0[1]=0x5f;a0[2]=0x80;a0[3]=60;
    a0[7]=0x0f;a0[9]=0x3c;a0[10]=0x41;a0[21]=0x80;
    const auto frame=response(a0,5);
    CHECK(ingest_ac_response(frame.data(),frame.size(),s)==ResponseKind::BasicState);
    CHECK(s.mode==4 && s.setpoint_c==27.5 && s.power && s.swing==15);
    CHECK(s.eco && s.purifier && s.aux_heat && s.dry && s.natural_wind);
    CHECK(s.freeze_protection_known && s.freeze_protection);
    CHECK(!s.has_indoor && !s.has_outdoor);
}
void subprotocol_and_queries() {
    // Captured upstream BB basic status, subtype 11 (midea-local fixture).
    const Bytes basic=hex(
        "aa5aac00000000000803bb5000ffff1101800000000057663200000000320001"
        "2800007804523266000400000000000000000000000000000000000000413c64"
        "0000002f000001e00000400003002828003000000000000046b7ef");
    State s;
    CHECK(ingest_ac_response(basic.data(),basic.size(),s)==ResponseKind::BasicState);
    CHECK(s.profile.family==AcProtocol::Subprotocol && s.profile.protocol_version==8);
    CHECK(s.power && s.mode==2 && s.setpoint_c==28.5 && s.fan==102);
    CHECK(!s.has_indoor && !s.has_outdoor);
    CHECK(ac_state_query(s.profile,7)==hex("aa12ac00000000000803aa0800ffff113d0237"));
    CHECK(ac_auxiliary_query(s.profile,7,0)==hex("aa12ac00000000000803aa0800ffff1063dd37"));
    CHECK(ac_auxiliary_query(s.profile,7,1)==hex("aa12ac00000000000803aa0800ffff3040e037"));
    const Bytes outdoor=hex(
        "aa6aac00000000000803bb6000ffff3000ff000000a60e8e12433131c000079b"
        "0000000000000000000000630000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "002f000000000000e5e6df");
    CHECK(ingest_ac_response(outdoor.data(),outdoor.size(),s)==ResponseKind::Auxiliary);
    CHECK(s.mode==2 && s.setpoint_c==28.5 && s.has_outdoor && s.outdoor_c==37.5);
    const Bytes late_c0=hex("aa1eac00000000000003c0004b1e7f7f000000000069630000000000000d33");
    CHECK(ingest_ac_response(late_c0.data(),late_c0.size(),s)==ResponseKind::BasicState);
    CHECK(s.profile.family==AcProtocol::Subprotocol); // A late classic reply must not change BB writes.
    AcProfile legacy;legacy.protocol_version=8;
    CHECK(ac_state_query(legacy,7)==hex("aa20ac00000000000803418100ff000000000000000000000000000000000776eb"));
    CHECK(subtype_query()==hex("aa1dac000000000000a00000000000000000000000000000000000000097"));
}
void properties_and_atomicity() {
    State s;s.mode=4;s.setpoint_c=22;s.power=true;
    const auto frame=response({0xb1,4,0x17,0,0,1,100,9,0,0,1,25,
                               0x39,0,0,1,1,0x15,0,0,1,53});
    CHECK(ingest_ac_response(frame.data(),frame.size(),s)==ResponseKind::Auxiliary);
    CHECK(s.display && s.vertical_angle==25 && s.self_clean && s.has_humidity);
    CHECK(s.indoor_humidity==53 && s.mode==4 && s.setpoint_c==22);
    CHECK((s.properties_known&(ControlDisplay|ControlVerticalAngle|ControlSelfClean))==
          (ControlDisplay|ControlVerticalAngle|ControlSelfClean));
    // A TLV claiming two bytes but carrying only one must not apply earlier tags.
    const auto bad=response({0xb1,2,0x17,0,0,1,0,9,0,0,2,50});
    CHECK(ingest_ac_response(bad.data(),bad.size(),s)==ResponseKind::Invalid);
    CHECK(s.display && s.vertical_angle==25);
    for(size_t n=0;n<frame.size();++n)
        CHECK(ingest_ac_response(frame.data(),n,s)==ResponseKind::Invalid);
    auto corrupt=frame;corrupt.back()^=1;
    CHECK(ingest_ac_response(corrupt.data(),corrupt.size(),s)==ResponseKind::Invalid);
    CHECK(s.display && s.mode==4);
}
State heat_state() {
    State s;s.mode=4;s.setpoint_c=21;s.power=true;s.fan=102;
    s.profile.protocol_version=8;
    s.freeze_protection_known=true;
    s.fahrenheit_known=s.follow_me_known=true;
    s.independent_aux_heat_known=s.target_humidity_known=true;
    s.comfort_mode_known=true;
    s.properties_known=ControlEco|ControlTurbo|ControlSleep|ControlAnion|ControlFreezeProtection;
    return s;
}
void controls_and_preservation() {
    State s=heat_state();s.freeze_protection_known=false;
    s.properties_known&=~ControlFreezeProtection;
    ControlPatch patch;patch.changed=ControlTemperature;patch.setpoint_c=22;
    Bytes frame{0x55};
    CHECK(!ac_set_state_command(s,patch,1,frame) && frame==Bytes{0x55});
    // Explicit choice resolves the otherwise unknown full-state freeze byte.
    patch.changed|=ControlFreezeProtection;patch.freeze_protection=false;
    CHECK(ac_set_state_command(s,patch,1,frame));
    if(frame.size()>31)CHECK(frame[31]==0 && frame[8]==8);
    patch.changed=ControlTemperature;
    const auto unsupported=response({0xb5,1,0x13,2,1,0});
    CHECK(ingest_ac_response(unsupported.data(),unsupported.size(),s)==ResponseKind::Capabilities);
    CHECK(s.freeze_protection_known && !s.freeze_protection);
    CHECK(ac_set_state_command(s,patch,2,frame));
    // Capability support alone is not the preset's live state.
    const auto supported=response({0xb5,1,0x13,2,1,1});
    State unknown=heat_state();unknown.freeze_protection_known=false;
    CHECK(ingest_ac_response(supported.data(),supported.size(),unknown)==ResponseKind::Capabilities);
    CHECK(!unknown.freeze_protection_known);
    CHECK(!ac_set_state_command(unknown,patch,2,frame));
    CHECK(ingest_ac_response(supported.data(),supported.size(),s)==ResponseKind::Capabilities);
    CHECK(!s.freeze_protection_known); // Do not retain an inference contradicted by new B5 data.
    s=heat_state();s.dry=true;s.power_saving=true;s.natural_wind=true;s.comfort_mode=true;
    s.eco=true;s.sleep=true;s.purifier=true;s.turbo=true;s.freeze_protection=true;
    CHECK(ac_set_state_command(s,patch,3,frame));
    if(frame.size()>32) {
        CHECK((frame[18]&0x28)==0x28 && (frame[19]&0xa4)==0xa4);
        CHECK((frame[20]&3)==3 && (frame[27]&0x40)!=0);
        CHECK((frame[31]&0x80)!=0 && (frame[32]&1)!=0);
    }
    State after=s;after.setpoint_c=22;
    CHECK(ac_control_confirmed(s,after,patch));
    after.freeze_protection_known=false;
    CHECK(!ac_control_confirmed(s,after,patch));
    after=s;after.setpoint_c=22;after.eco=false;
    CHECK(!ac_control_confirmed(s,after,patch));
    s.smart_eye=true;after=s;after.setpoint_c=22;after.smart_eye=false;
    CHECK(!ac_control_confirmed(s,after,patch));
    patch.changed=ControlTemperature;patch.setpoint_c=22.5;
    CHECK(!valid_ac_control_patch(s,patch)); // Unknown resolution defaults to 1 C.
    const auto half=response({0xb5,1,0x25,2,7,32,60,32,60,32,60,1});
    CHECK(ingest_ac_response(half.data(),half.size(),s)==ResponseKind::Capabilities);
    CHECK(valid_ac_control_patch(s,patch));
    patch.setpoint_c=31;
    CHECK(!valid_ac_control_patch(s,patch));
}
void property_and_bb_controls() {
    State s=heat_state();s.freeze_protection_known=false;
    s.properties_known=ControlDisplay|ControlVerticalAngle;
    ControlPatch p;p.changed=ControlDisplay|ControlVerticalAngle;p.display=true;p.vertical_angle=25;
    Bytes frame;
    CHECK(ac_set_state_command(s,p,9,frame));
    CHECK(frame==hex("aa16ac00000000000802b0021700016409000119098159"));
    State after=s;after.display=true;after.vertical_angle=25;
    CHECK(ac_control_confirmed(s,after,p));
    after.properties_known&=~ControlVerticalAngle;
    CHECK(!ac_control_confirmed(s,after,p));
    p.changed|=ControlTemperature;p.setpoint_c=22;
    CHECK(!ac_set_state_command(s,p,9,frame));
    p.changed=ControlVerticalAngle;p.vertical_angle=26;
    CHECK(!valid_ac_control_patch(s,p));
    s=heat_state();s.profile.family=AcProtocol::Subprotocol;
    s.freeze_protection_known=false;s.bb_timer_known=true;
    p={};p.changed=ControlTemperature;p.setpoint_c=22;
    CHECK(ac_set_state_command(s,p,9,frame));
    CHECK(frame==hex("aa37ac00000000000802aa2d00ffff200380000000034a6632000000000000010100015c004a3266000000000000000000000000080a5013"));
    p.changed=ControlSwing;p.swing=12;
    CHECK(!valid_ac_control_patch(s,p));
    p.changed=ControlFreezeProtection;p.freeze_protection=false;
    CHECK(!valid_ac_control_patch(s,p));
}
void identified_temperature_layout_and_subtype() {
    State s=heat_state();s.profile.model_known=true;s.profile.new_temperature_layout=true;
    const char model[]="22013279";
    std::copy(model,model+sizeof(model),s.profile.model.begin());
    const Bytes c0=hex("aa1eac00000000000003c0004b1e7f7f000000000069630000000000000d33");
    CHECK(ingest_ac_response(c0.data(),c0.size(),s)==ResponseKind::BasicState);
    CHECK(!valid_state(s) && !s.has_indoor && !s.has_outdoor);
    Bytes value(42,0);value[1]=0x4d;value[40]=107;value[41]=3;
    Bytes body{0xb1,1,0x7e,0,0,42};body.insert(body.end(),value.begin(),value.end());
    const auto extended=response(body);
    CHECK(ingest_ac_response(extended.data(),extended.size(),s)==ResponseKind::Auxiliary);
    CHECK(valid_state(s) && s.setpoint_c==18.5 && s.has_indoor && std::fabs(s.indoor_c-28.8)<0.001);
    CHECK(!s.has_outdoor);
    const auto query=ac_auxiliary_query(s.profile,3,2);
    CHECK(query.size()>14 && query[11]==11 && query[query.size()-5]==0x7e && query[query.size()-4]==0);
    State other=heat_state();other.profile.new_temperature_layout=true;
    CHECK(ingest_ac_response(extended.data(),extended.size(),other)==ResponseKind::Auxiliary);
    CHECK(other.setpoint_c==21 && !other.has_indoor); // Unknown model must keep standard mapping.
    AcProfile profile;profile.subtype_known=true;profile.subtype=32773;
    auto appliance=response(Bytes(19,0),0xa0,8);
    CHECK(parse_subtype(appliance.data(),appliance.size(),profile));
    CHECK(profile.protocol_version==8 && profile.subtype_known && profile.subtype==32773);
    State negotiated=heat_state();negotiated.profile=profile;
    CHECK(ingest_ac_response(c0.data(),c0.size(),negotiated)==ResponseKind::BasicState);
    CHECK(negotiated.profile.protocol_version==8); // Appliance reply owns negotiated version.
    appliance.back()^=1;
    CHECK(!parse_subtype(appliance.data(),appliance.size(),profile));
    CHECK(profile.protocol_version==8 && profile.subtype==32773);
}
void mode_change_checks_inherited_target() {
    State s=heat_state();s.setpoint_c=29;
    s.profile.capabilities.temperatures[2]={true,16,25};
    ControlPatch patch;patch.changed=ControlMode;patch.mode=2;
    CHECK(!valid_ac_control_patch(s,patch));
    patch.changed|=ControlTemperature;patch.setpoint_c=24;
    CHECK(valid_ac_control_patch(s,patch));
    // An explicit self-clean request may have B1 support evidence without B5.
    CHECK(!ac_auxiliary_query(s.profile,2,4).empty());
    CHECK(ac_auxiliary_query(s.profile,2,5)==hex("aa10ac00000000000803b101480002a598"));
    // This independent group supplies state for an explicit rate-select
    // request without adding an unadvertised tag to normal B1 refreshes.
    const auto normal=ac_auxiliary_query(s.profile,2,2);
    CHECK(normal.size()>12 && normal[11]==10);
}
Bytes classic_body(bool alternate=false,size_t length=23) {
    Bytes body(length,0);
    body[0]=alternate?0xa0:0xc0;
    body[1]=alternate?19:1; // Heat, 21 C, power on in each response layout.
    body[2]=alternate?0x80:0x85;body[3]=102;
    body[11]=body[12]=0xff;
    if(!alternate && length>19)body[19]=60;
    return body;
}
void classic_preservation_evidence() {
    ControlPatch patch;patch.changed=ControlTemperature;patch.setpoint_c=22;
    const auto a0=response(classic_body(true),5);
    State alternate;
    CHECK(ingest_ac_response(a0.data(),a0.size(),alternate)==ResponseKind::BasicState);
    CHECK(!alternate.fahrenheit_known && !alternate.follow_me_known &&
          !alternate.independent_aux_heat_known && !alternate.target_humidity_known);
    CHECK(alternate.comfort_mode_known);
    Bytes frame{0x55};
    // A0 does not report the target humidity, Follow Me, independent heater,
    // or Fahrenheit fields that a complete 0x40 command would overwrite.
    CHECK(!ac_set_state_command(alternate,patch,1,frame) && frame==Bytes{0x55});
    State after=alternate;after.setpoint_c=22;
    CHECK(!ac_control_confirmed(alternate,after,patch));

    const auto c0=response(classic_body());
    State complete;
    CHECK(ingest_ac_response(c0.data(),c0.size(),complete)==ResponseKind::BasicState);
    CHECK(complete.fahrenheit_known && complete.follow_me_known &&
          complete.independent_aux_heat_known && complete.target_humidity_known);
    CHECK(complete.comfort_mode_known);
    CHECK(ac_set_state_command(complete,patch,1,frame));
    after=complete;after.setpoint_c=22;
    CHECK(ac_control_confirmed(complete,after,patch));
    after.humidity=40;
    CHECK(!ac_control_confirmed(complete,after,patch));
    for(const auto field:{&State::fahrenheit_known,&State::follow_me_known,
                          &State::independent_aux_heat_known,&State::target_humidity_known,
                          &State::comfort_mode_known}) {
        State incomplete=complete;incomplete.*field=false;
        CHECK(!ac_set_state_command(incomplete,patch,1,frame));
        after=incomplete;after.setpoint_c=22;
        CHECK(!ac_control_confirmed(complete,after,patch));
    }
    State later=complete;
    CHECK(ingest_ac_response(a0.data(),a0.size(),later)==ResponseKind::BasicState);
    CHECK(!later.fahrenheit_known && !later.follow_me_known &&
          !later.independent_aux_heat_known && !later.target_humidity_known);

    const auto short_c0=response(classic_body(false,19));
    State short_state;
    CHECK(ingest_ac_response(short_c0.data(),short_c0.size(),short_state)==ResponseKind::BasicState);
    CHECK(short_state.fahrenheit_known && short_state.follow_me_known &&
          short_state.independent_aux_heat_known && !short_state.target_humidity_known);
    patch.changed|=ControlFreezeProtection;patch.freeze_protection=false;
    CHECK(!ac_set_state_command(short_state,patch,1,frame));
    const auto unsupported=response({0xb5,1,0x13,2,1,0});
    CHECK(ingest_ac_response(unsupported.data(),unsupported.size(),short_state)==ResponseKind::Capabilities);
    patch.changed=ControlTemperature;
    CHECK(!ac_set_state_command(short_state,patch,1,frame));

    auto ambiguous_body=classic_body();ambiguous_body[8]=0x40;
    const auto ambiguous=response(ambiguous_body);
    State unresolved;
    CHECK(ingest_ac_response(ambiguous.data(),ambiguous.size(),unresolved)==ResponseKind::BasicState);
    CHECK(unresolved.classic_aux_bit_ambiguous && !unresolved.independent_aux_heat_known);
    CHECK(!unresolved.smart_eye); // Never turn both conflicting interpretations on.
    CHECK(!ac_set_state_command(unresolved,patch,1,frame));
    after=unresolved;after.setpoint_c=22;
    CHECK(!ac_control_confirmed(unresolved,after,patch));
    // The conflicting whole-state bit does not prevent a selective B0 write.
    const auto display=response({0xb1,1,0x17,0,0,1,0});
    CHECK(ingest_ac_response(display.data(),display.size(),unresolved)==ResponseKind::Auxiliary);
    patch={};patch.changed=ControlDisplay;patch.display=true;
    CHECK(ac_set_state_command(unresolved,patch,1,frame));
    after=unresolved;after.display=true;
    CHECK(ac_control_confirmed(unresolved,after,patch));
}
void absolute_display_requires_property_evidence() {
    State s=heat_state();
    const auto generic=response({0xb5,1,0x24,2,1,1});
    CHECK(ingest_ac_response(generic.data(),generic.size(),s)==ResponseKind::Capabilities);
    CHECK(s.profile.capabilities.supports(AcFeature::Display));
    ControlPatch patch;patch.changed=ControlDisplay;patch.display=true;
    Bytes frame{0x55};
    CHECK(!valid_ac_control_patch(s,patch));
    CHECK(!ac_set_state_command(s,patch,1,frame) && frame==Bytes{0x55});
    const auto property=response({0xb1,1,0x17,0,0,1,0});
    CHECK(ingest_ac_response(property.data(),property.size(),s)==ResponseKind::Auxiliary);
    CHECK(ac_set_state_command(s,patch,1,frame));
}
void classic_comfort_tail_preservation() {
    // C0 can include target humidity and frost while ending before comfort
    // byte 22. A default false must not become an observed preset value.
    const auto short_tail=response(classic_body(false,22));
    State incomplete;
    CHECK(ingest_ac_response(short_tail.data(),short_tail.size(),incomplete)==ResponseKind::BasicState);
    CHECK(incomplete.target_humidity_known && incomplete.freeze_protection_known);
    CHECK(!incomplete.comfort_mode_known);
    ControlPatch patch;patch.changed=ControlTemperature;patch.setpoint_c=22;
    Bytes frame{0x55};
    CHECK(!ac_set_state_command(incomplete,patch,1,frame) && frame==Bytes{0x55});
    State after=incomplete;after.setpoint_c=22;
    CHECK(!ac_control_confirmed(incomplete,after,patch));

    const auto full=response(classic_body());
    State complete;
    CHECK(ingest_ac_response(full.data(),full.size(),complete)==ResponseKind::BasicState);
    CHECK(ac_set_state_command(complete,patch,1,frame));
    CHECK(!ac_control_confirmed(complete,after,patch));
    // Other presets still require an observation of the state they clear,
    // consistently with the unknown frost preservation policy.
    patch={};patch.changed=ControlEco;patch.eco=true;
    CHECK(!ac_set_state_command(incomplete,patch,1,frame));

    const auto property=response({0xb1,1,0x17,0,0,1,0});
    CHECK(ingest_ac_response(property.data(),property.size(),incomplete)==ResponseKind::Auxiliary);
    patch={};patch.changed=ControlDisplay;patch.display=true;
    CHECK(ac_set_state_command(incomplete,patch,1,frame));
}
void preset_and_dry_transitions() {
    // midea-local set_attribute clears the other presets when enabling one.
    for(const auto selected:{ControlEco,ControlTurbo,ControlSleep,ControlFreezeProtection}) {
        State before=heat_state();
        before.eco=before.turbo=before.sleep=before.freeze_protection=true;
        before.power_saving=before.comfort_mode=true;
        ControlPatch patch;patch.changed=selected;
        patch.eco=patch.turbo=patch.sleep=patch.freeze_protection=true;
        Bytes frame;
        CHECK(ac_set_state_command(before,patch,2,frame));
        if(frame.size()>32) {
            CHECK(((frame[18]&0x20)!=0)==(selected==ControlTurbo));
            CHECK((frame[18]&8)==0);
            CHECK(((frame[19]&0x80)!=0)==(selected==ControlEco));
            CHECK(((frame[20]&1)!=0)==(selected==ControlSleep));
            CHECK(((frame[31]&0x80)!=0)==(selected==ControlFreezeProtection));
            CHECK((frame[32]&1)==0);
        }
        State after=before;
        after.eco=selected==ControlEco;after.turbo=selected==ControlTurbo;
        after.sleep=selected==ControlSleep;after.freeze_protection=selected==ControlFreezeProtection;
        after.power_saving=after.comfort_mode=false;
        CHECK(ac_control_confirmed(before,after,patch));
        after=before;
        CHECK(!ac_control_confirmed(before,after,patch));
    }
    State before=heat_state();
    ControlPatch patch;patch.changed=ControlEco|ControlTurbo;patch.eco=patch.turbo=true;
    CHECK(!valid_ac_control_patch(before,patch));
    patch.eco=false;
    CHECK(valid_ac_control_patch(before,patch));

    // Selecting another preset does not turn an unknown frost bit into an
    // observation. The caller can explicitly resolve the bit in the patch.
    before.freeze_protection_known=false;
    patch={};patch.changed=ControlEco;patch.eco=true;
    Bytes frame;
    CHECK(!ac_set_state_command(before,patch,2,frame));
    patch.changed|=ControlFreezeProtection;patch.freeze_protection=false;
    CHECK(ac_set_state_command(before,patch,2,frame));
    before=heat_state();

    before.profile.family=AcProtocol::Subprotocol;before.bb_timer_known=true;
    before.eco=before.sleep=true;
    patch={};patch.changed=ControlTurbo;patch.turbo=true;
    CHECK(ac_set_state_command(before,patch,2,frame));
    if(frame.size()>41)CHECK((frame[16]&0x20)!=0 && (frame[18]&0x80)==0 && (frame[41]&0x40)==0);
    State after=before;after.turbo=true;after.eco=after.sleep=false;
    CHECK(ac_control_confirmed(before,after,patch));

    before=heat_state();before.mode=3;before.fan=40;before.dry=true;
    patch={};patch.changed=ControlMode;patch.mode=4;
    CHECK(ac_set_state_command(before,patch,2,frame));
    if(frame.size()>19)CHECK(frame[13]==102 && (frame[19]&4)==0);
    after=before;after.mode=4;after.dry=false;after.fan=102;
    CHECK(ac_control_confirmed(before,after,patch));
    after.fan=40;
    CHECK(!ac_control_confirmed(before,after,patch));
    patch.changed|=ControlFan;patch.fan=60;
    CHECK(ac_set_state_command(before,patch,2,frame));
    if(frame.size()>13)CHECK(frame[13]==60);
    after.fan=60;
    CHECK(ac_control_confirmed(before,after,patch));
}
void all_property_tags_and_freshness() {
    State before=heat_state();before.freeze_protection_known=false;
    before.properties_known=AcPropertyFields;
    ControlPatch patch;patch.changed=AcPropertyFields;
    patch.display=true;patch.vertical_angle=1;patch.horizontal_angle=100;patch.rate_select=80;
    patch.breezeless=true;patch.indirect_wind=true;patch.self_clean=true;patch.sound=false;
    Bytes frame;
    CHECK(ac_set_state_command(before,patch,10,frame));
    CHECK(frame==hex("aa2eac00000000000802b00817000164090001010a000164480001501800010142000102390001012c0201000a7191"));
    const auto properties=response({0xb1,8,0x17,0,0,1,100,9,0,0,1,1,
        10,0,0,1,100,0x48,0,0,1,80,0x18,0,0,1,1,0x42,0,0,1,2,
        0x39,0,0,1,1,0x2c,2,0,1,0});
    State actual=before;
    CHECK(ingest_ac_response(properties.data(),properties.size(),actual)==ResponseKind::Auxiliary);
    CHECK(ac_control_confirmed(before,actual,patch));
    const auto notice=response({0xb5,1,0x39,0,1,1},5);
    State clean=heat_state();
    CHECK(ingest_ac_response(notice.data(),notice.size(),clean)==ResponseKind::Auxiliary);
    CHECK(!clean.self_clean && !(clean.properties_known&ControlSelfClean));
    const Bytes short_c0=hex("aa1eac00000000000003c0004b1e7f7f000000000069630000000000000d33");
    CHECK(ingest_ac_response(short_c0.data(),short_c0.size(),actual)==ResponseKind::BasicState);
    CHECK((actual.properties_known&AcPropertyFields)==0);
    CHECK(!ac_control_confirmed(before,actual,patch));
    // Every physical SET requires current BB timer information, even though
    // normal BB climate control does not serialize a freeze-protection bit.
    State bb=heat_state();bb.profile.family=AcProtocol::Subprotocol;
    bb.bb_timer_known=true;bb.bb_timer=true;
    patch={};patch.changed=ControlTemperature;patch.setpoint_c=22;
    CHECK(!ac_set_state_command(bb,patch,3,frame));
    bb.bb_sn8_known=true;bb.bb_sn8=true;bb.eco=true;
    CHECK(ac_set_state_command(bb,patch,3,frame));
    if(frame.size()>41)CHECK(frame[41]==0x44);
}
void bounded_malformed_packets() {
    // Valid outer framing drives all body parsers with short, contradictory
    // payloads. The sanitizer run checks memory bounds, and failed decoding
    // must not mutate the caller's last accepted state.
    uint32_t random=0x8ac351u;
    for(const uint8_t opcode:{0xa0,0xc0,0xb0,0xb1,0xb5,0xbb}) {
        for(size_t length=1;length<=96;++length) {
            Bytes body(length);
            for(auto& byte:body) {
                random=random*1664525u+1013904223u;byte=static_cast<uint8_t>(random>>24);
            }
            body[0]=opcode;
            const auto frame=response(body,opcode==0xa0?5:3);
            State s=heat_state();s.properties_known=AcPropertyFields;
            const auto result=ingest_ac_response(frame.data(),frame.size(),s);
            if(result==ResponseKind::Invalid) {
                CHECK(s.power && s.mode==4 && s.setpoint_c==21);
                CHECK(s.properties_known==AcPropertyFields);
                CHECK(s.profile.family==AcProtocol::Legacy && s.profile.protocol_version==8);
            }
        }
    }
    State s=heat_state();
    CHECK(ingest_ac_response(nullptr,256,s)==ResponseKind::Invalid);
    CHECK(s.setpoint_c==21 && s.mode==4);
}
}
int main() {
    basic_families();
    subprotocol_and_queries();
    properties_and_atomicity();
    controls_and_preservation();
    property_and_bb_controls();
    identified_temperature_layout_and_subtype();
    mode_change_checks_inherited_target();
    classic_preservation_evidence();
    absolute_display_requires_property_evidence();
    classic_comfort_tail_preservation();
    preset_and_dry_transitions();
    all_property_tags_and_freshness();
    bounded_malformed_packets();
    if(failures)return 1;
    std::cout<<"Midea capability-driven AC protocol tests PASS\n";
}
