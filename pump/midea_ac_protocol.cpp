#include "pump/midea_ac_protocol.hpp"
#include <cmath>
#include <cstring>
#include <utility>

// Protocol references (MIT): midea-lan/midea-local 15.2.1, commit
// 425bba4a05b74b619d31bf2d8b5a6baf030aa042, devices/ac/message.py;
// mill1000/midea-msmart 2026.11.0, commit dd0994755bb078b613837038f2d67d97fab133ab,
// msmart/device/AC/command.py. See THIRD_PARTY_NOTICES.md for licenses.
// Model 220F4047 C0 temperatures: wuwentao/midea-lan, commit
// e414af70d57cd7fbbac1ddcf50e700077478a28c, midealan/devices/ac/__init__.py.
namespace enevina::midea {
namespace {
uint8_t crc8(const uint8_t* bytes,size_t n) {
    uint8_t crc=0;
    for(size_t i=0;i<n;++i) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;++bit)crc=(crc&1)?(crc>>1)^0x8c:crc>>1;
    }
    return crc;
}
uint8_t checksum(const uint8_t* bytes,size_t n) {
    uint8_t sum=0;
    for(size_t i=0;i<n;++i)sum+=bytes[i];
    return static_cast<uint8_t>(-sum);
}
bool outer_valid(const uint8_t* frame,size_t n) {
    return frame && n>=12 && n<=256 && frame[0]==0xaa && frame[2]==0xac &&
        static_cast<size_t>(frame[1])+1==n && checksum(frame+1,n-2)==frame[n-1];
}
bool legacy_body(const uint8_t* frame,size_t n,const uint8_t*& p,size_t& size) {
    if(!outer_valid(frame,n))return false;
    p=frame+10;size=n-12;
    return crc8(p,size)==p[size] || checksum(p,size)==p[size];
}
Bytes envelope(Bytes body,uint8_t type,uint8_t protocol) {
    if(body.size()>245)return {};
    Bytes frame{0xaa,static_cast<uint8_t>(10+body.size()),0xac,0,0,0,0,0,protocol,type};
    frame.insert(frame.end(),body.begin(),body.end());
    frame.push_back(checksum(frame.data()+1,frame.size()-1));
    return frame;
}
Bytes classic_command(Bytes body,uint8_t id,uint8_t type,uint8_t protocol) {
    body.push_back(id);
    body.push_back(crc8(body.data(),body.size()));
    return envelope(std::move(body),type,protocol);
}
Bytes bb_command(uint8_t subtype,const Bytes& data,uint8_t type,uint8_t protocol) {
    if(data.size()>237)return {};
    Bytes body{0xaa,static_cast<uint8_t>(8+data.size()),0,0xff,0xff,subtype};
    body.insert(body.end(),data.begin(),data.end());
    body.push_back(crc8(body.data(),body.size()));
    body.push_back(checksum(body.data(),body.size()));
    return envelope(std::move(body),type,protocol);
}
void unsupported_frost(State& s) {
    if(s.freeze_protection_inferred) {
        s.freeze_protection_known=false;
        s.freeze_protection_inferred=false;
        s.properties_known&=~ControlFreezeProtection;
    }
    if(!s.freeze_protection_known &&
       s.profile.capabilities.reports(AcFeature::FreezeProtection) &&
       !s.profile.capabilities.supports(AcFeature::FreezeProtection)) {
        s.freeze_protection=false;
        s.freeze_protection_known=true;
        s.freeze_protection_inferred=true;
        s.properties_known|=ControlFreezeProtection;
    }
}
bool uses_new_temperatures(const AcProfile& profile) {
    return profile.new_temperature_layout && profile.model_known &&
        std::memcmp(profile.model.data(),"22013279",8)==0 && profile.model[8]==0;
}
bool uses_c0_temperature_correction(const AcProfile& profile) {
    return profile.model_known && profile.subtype_known && profile.subtype==8 &&
        std::memcmp(profile.model.data(),"220F4047",8)==0 && profile.model[8]==0;
}
bool parse_classic(const uint8_t* frame,size_t n,State& state) {
    const uint8_t* p=nullptr;size_t length=0;
    if(!legacy_body(frame,n,p,length))return false;
    const bool c0=p[0]==0xc0 && (frame[9]==2 || frame[9]==3);
    const bool a0=p[0]==0xa0 && (frame[9]==5 || frame[9]==3);
    if((!a0 && !c0) || length<17)return false;
    State next{};
    next.profile=state.profile;
    if(!next.profile.protocol_version_known)next.profile.protocol_version=frame[8];
    if(c0) {
        if(!parse_state(frame,n,next))return false;
        next.profile=state.profile;
        if(!next.profile.protocol_version_known)next.profile.protocol_version=frame[8];
    } else {
        next.power=(p[1]&1)!=0;
        next.mode=(p[2]>>5)&7;
        next.setpoint_c=((p[1]&0x3e)>>1)+12.0+((p[1]&0x40)?0.5:0.0);
        next.fan=p[3]&0x7f;next.swing=p[7]&15;
        next.turbo=(p[8]&0x20)!=0 || (p[10]&2)!=0;
        next.eco=(p[9]&0x10)!=0;next.purifier=(p[9]&0x20)!=0;
        next.aux_heat=(p[9]&8)!=0;next.sleep=(p[10]&1)!=0;
        next.natural_wind=(p[10]&0x40)!=0;next.smart_eye=(p[9]&1)!=0;
        next.comfort_mode=(p[14]&1)!=0;next.comfort_mode_known=true;
        if(length>21) {
            next.freeze_protection_known=true;
            next.freeze_protection=(p[21]&0x80)!=0;
        }
    }
    next.dry=(p[9]&4)!=0;
    next.power_saving=(p[8]&8)!=0;
    if(c0) {
        next.natural_wind=(p[9]&2)!=0;
        if(uses_c0_temperature_correction(next.profile)) {
            // midea_ac_lan#998: this exact model/subtype omits the +50 offset.
            // Upstream body includes the inner CRC, so its len(body)>20
            // decimal guard means at least 20 content bytes here.
            const double decimal=length>=20?(p[15]&0x0f)/10.0:0.0;
            next.has_indoor=p[11]!=0xff;
            next.indoor_c=next.has_indoor?p[11]/2.0+decimal:0.0;
            if(p[12]==0x20) { next.has_outdoor=false;next.outdoor_c=0; }
        }
        if(length>22) {
            next.comfort_mode=(p[22]&1)!=0;next.comfort_mode_known=true;
        }
    }
    next.properties_known=ControlPower|ControlMode|ControlTemperature|ControlFan|
        ControlSwing|ControlEco|ControlTurbo|ControlSleep|ControlAnion;
    if(next.freeze_protection_known)next.properties_known|=ControlFreezeProtection;
    next.display=(p[14]&0x70)!=0x70;
    // C0 display is the legacy toggle state. B1/17 is the evidence for an
    // absolute property control, so do not confuse it with a B1 observation.
    unsupported_frost(next);
    next.has_basic=true;
    if(!valid_state(next))return false;
    if(c0 && uses_new_temperatures(next.profile)) {
        // This verified model returns stale C0 temperature fields. Keep the
        // fresh basic flags, but make state invalid for control until 0x7e
        // arrives in the same acquisition; never reuse a previous session.
        next.setpoint_c=0;next.has_indoor=false;next.has_outdoor=false;
        next.indoor_c=0;next.outdoor_c=0;
    }
    state=next;
    return true;
}
ResponseKind parse_bb(const uint8_t* frame,size_t n,State& state) {
    if(!outer_valid(frame,n) || (frame[9]!=2 && frame[9]!=3 && frame[9]!=5))
        return ResponseKind::Invalid;
    const uint8_t* body=frame+10;
    const size_t size=n-11;
    if(body[0]!=0xbb || size<21 || body[1]!=size ||
       crc8(body,size-2)!=body[size-2] ||
       checksum(body,size-1)!=body[size-1])return ResponseKind::Invalid;
    const uint8_t* p=body+6;
    const size_t length=size-8;
    const uint8_t kind=body[5];
    State next=state;
    if(kind==0x11 || kind==0x20 || kind==0xf1) {
        if(length<8)return ResponseKind::Invalid;
        next=State{};next.profile=state.profile;
        next.power=(p[0]&1)!=0;next.dry=(p[0]&0x10)!=0;next.turbo=(p[0]&0x20)!=0;
        next.aux_heat=(p[1]&0x40)!=0;next.sleep=(p[2]&0x80)!=0;
        next.follow_me=(p[4]&0x40)!=0;next.follow_me_known=true;
        constexpr uint8_t modes[]{0,3,1,2,4,5};
        next.mode=0xff;
        for(uint8_t mode=1;mode<=5;++mode)if(modes[mode]==p[5]+1)next.mode=mode;
        next.setpoint_c=(static_cast<int>(p[6])-30)/2.0;
        next.fan=p[7];
        next.properties_known=ControlPower|ControlMode|ControlTemperature|ControlFan|
            ControlTurbo|ControlSleep;
        if(length>25) {
            next.eco=(p[25]&0x40)!=0;next.bb_timer=(p[25]&4)!=0;
            next.bb_timer_known=true;next.properties_known|=ControlEco;
        }
        next.has_basic=true;
        if(!valid_state(next))return ResponseKind::Invalid;
    } else if(kind==0x10) {
        if(length<9)return ResponseKind::Invalid;
        const auto raw=static_cast<int16_t>(static_cast<uint16_t>(p[7])|
                                           (static_cast<uint16_t>(p[8])<<8));
        next.indoor_c=raw/100.0;
        next.has_indoor=next.indoor_c>-60 && next.indoor_c<80;
        next.has_humidity=false;
        if(length>30 && p[30]>=1 && p[30]<=100) {
            next.indoor_humidity=p[30];next.has_humidity=true;
        }
        next.bb_sn8_known=length>80;
        next.bb_sn8=next.bb_sn8_known && p[80]==0x31;
    } else if(kind==0x30) {
        if(length<7)return ResponseKind::Invalid;
        const auto raw=static_cast<int16_t>(static_cast<uint16_t>(p[5])|
                                           (static_cast<uint16_t>(p[6])<<8));
        next.outdoor_c=raw/100.0;
        next.has_outdoor=next.outdoor_c>-80 && next.outdoor_c<80;
    } else return ResponseKind::Invalid;
    next.profile.family=AcProtocol::Subprotocol;
    if(!next.profile.protocol_version_known)next.profile.protocol_version=frame[8];
    state=next;
    return (kind==0x11 || kind==0x20 || kind==0xf1)?ResponseKind::BasicState:
                                                               ResponseKind::Auxiliary;
}
bool parse_properties(const uint8_t* frame,size_t n,State& state) {
    const uint8_t* p=nullptr;size_t length=0;
    if(!legacy_body(frame,n,p,length) || length<2 ||
       (frame[9]!=2 && frame[9]!=3 && frame[9]!=5) ||
       (p[0]!=0xb0 && p[0]!=0xb1 && !(p[0]==0xb5 && frame[9]==5)))return false;
    const bool capability_notice=p[0]==0xb5;
    const size_t header=capability_notice?3:4;
    State next=state;
    size_t cursor=2;
    for(unsigned index=0;index<p[1];++index) {
        if(cursor+header>length)return false;
        const uint16_t tag=static_cast<uint16_t>(p[cursor])|
                           static_cast<uint16_t>(p[cursor+1])<<8;
        const size_t count=p[cursor+header-1];
        cursor+=header;
        if(count>length-cursor)return false;
        const uint8_t* value=p+cursor;
        cursor+=count;
        if(!count)continue;
        if(tag==0x007e && uses_new_temperatures(next.profile)) {
            if(count<42)return false;
            double target=11.5+(value[1]&0x3f)/2.0+((value[1]&0x40)?0.5:0.0);
            if(target<10 || target>40)target=(static_cast<int>(value[3])-50)/2.0;
            const double indoor=(static_cast<int>(value[40])-50)/2.0+value[41]/10.0;
            if(target<10 || target>40 || indoor<10 || indoor>40)return false;
            next.setpoint_c=target;next.indoor_c=indoor;
            next.has_indoor=true;next.has_outdoor=false;
            next.properties_known|=ControlTemperature;
            continue;
        }
        if(capability_notice)continue;
        switch(tag) {
        case 0x0017:next.display=value[0]>0;next.properties_known|=ControlDisplay;break;
        case 0x0009:next.vertical_angle=value[0];next.properties_known|=ControlVerticalAngle;break;
        case 0x000a:next.horizontal_angle=value[0];next.properties_known|=ControlHorizontalAngle;break;
        case 0x0048:next.rate_select=value[0];next.properties_known|=ControlRateSelect;break;
        case 0x0018:next.breezeless=value[0]==1;next.properties_known|=ControlBreezeless;break;
        case 0x0042:next.indirect_wind=value[0]==2;next.properties_known|=ControlIndirectWind;break;
        case 0x0039:next.self_clean=value[0]>0;next.properties_known|=ControlSelfClean;break;
        case 0x022c:next.sound=value[0]>0;next.properties_known|=ControlSound;break;
        case 0x0015:
            next.has_humidity=value[0]>=1 && value[0]<=100;
            next.indoor_humidity=next.has_humidity?value[0]:0;
            break;
        default:break;
        }
    }
    // Responses may append a message serial and a second checksum/status
    // trailer. Never use those bytes as part of the final TLV value.
    const size_t trailing=length-cursor;
    if(trailing!=0 && trailing!=1 && trailing!=2 && trailing!=5 && trailing!=6)
        return false;
    if(!next.profile.protocol_version_known)next.profile.protocol_version=frame[8];
    state=next;
    return true;
}
} // namespace

ResponseKind ingest_ac_response(const uint8_t* frame,size_t n,State& state) {
    if(parse_classic(frame,n,state))return ResponseKind::BasicState;
    const auto bb=parse_bb(frame,n,state);
    if(bb!=ResponseKind::Invalid)return bb;
    if(parse_properties(frame,n,state))return ResponseKind::Auxiliary;
    AcProfile profile=state.profile;
    if(parse_subtype(frame,n,profile)) {
        state.profile=profile;return ResponseKind::Subtype;
    }
    auto caps=state.profile.capabilities;
    if(parse_capabilities(frame,n,caps)) {
        state.profile.capabilities=caps;
        if(!state.profile.protocol_version_known)state.profile.protocol_version=frame[8];
        unsupported_frost(state);
        return ResponseKind::Capabilities;
    }
    return ResponseKind::Invalid;
}
Bytes subtype_query(uint8_t protocol) {
    return envelope(Bytes(19,0),0xa0,protocol);
}
bool parse_subtype(const uint8_t* frame,size_t n,AcProfile& profile) {
    if(!outer_valid(frame,n) || frame[9]!=0xa0 || n<30)return false;
    // Appliance query resolves the AA message protocol version. Subtype/model
    // come from verified discovery metadata, not an invented body offset.
    profile.protocol_version=frame[8];
    profile.protocol_version_known=true;
    return true;
}
Bytes ac_state_query(const AcProfile& profile,uint8_t id) {
    if(profile.family==AcProtocol::Subprotocol)
        return bb_command(0x11,{},3,profile.protocol_version);
    Bytes body(20,0);body[0]=0x41;body[1]=0x81;body[3]=0xff;
    return classic_command(std::move(body),id,3,profile.protocol_version);
}
Bytes ac_auxiliary_query(const AcProfile& profile,uint8_t id,uint8_t group) {
    if(group<2) {
        if(profile.family!=AcProtocol::Subprotocol)return {};
        return bb_command(group==0?0x10:0x30,{},3,profile.protocol_version);
    }
    if(group==3) {
        if(profile.family!=AcProtocol::Legacy)return {};
        return classic_command({0xa0,0xa7},id,3,profile.protocol_version);
    }
    if(group==4) {
        return classic_command({0xb1,1,0x39,0},id,3,profile.protocol_version);
    }
    if(group==5) {
        return classic_command({0xb1,1,0x48,0},id,3,profile.protocol_version);
    }
    if(group!=2)return {};
    // The normal upstream B1 group deliberately omits error-code and self-clean
    // tags: unsupported tags can suppress an otherwise useful entire reply.
    Bytes body{0xb1,10,0x42,0,0x18,0,0x15,0,0x17,0,0x33,2,0x4b,0,
               0x0a,0,0x09,0,0xcd,0,0x2c,2};
    if(profile.capabilities.supports(AcFeature::RateSelect)) {
        ++body[1];body.push_back(0x48);body.push_back(0);
    }
    if(uses_new_temperatures(profile)) {
        ++body[1];body.push_back(0x7e);body.push_back(0);
    }
    return classic_command(std::move(body),id,3,profile.protocol_version);
}
namespace {
bool feature_available(const State& state,AcFeature feature,uint32_t field) {
    return state.profile.capabilities.supports(feature,(state.properties_known&field)!=0);
}
State classic_command_defaults(State state) {
    // Match msmart AirConditioner.apply() for fields absent from a short C0
    // or A0. This copy is only the outgoing command, never an observation.
    // midea-local likewise defaults its unreported frost/comfort flags off.
    if(!state.fahrenheit_known)state.fahrenheit=false;
    if(!state.follow_me_known)state.follow_me=false;
    if(!state.independent_aux_heat_known)state.independent_aux_heat=false;
    if(!state.target_humidity_known)state.humidity=40;
    if(!state.comfort_mode_known)state.comfort_mode=false;
    if(!state.freeze_protection_known)state.freeze_protection=false;
    return state;
}
template<class T>
bool observed_optional_matches(bool expected_known,bool actual_known,T expected,T actual) {
    // Losing a previously observed field cannot prove preservation. A field
    // absent from both reads cannot veto a confirmed basic-state command.
    // If a field appears after SET, check it against the value we encoded.
    return (!expected_known || actual_known) && (!actual_known || expected==actual);
}
bool angle_value(uint8_t value) {
    return value==0 || value==1 || value==25 || value==50 || value==75 || value==100;
}
bool rate_value(const AcCapabilities& caps,uint8_t value) {
    if(caps.supports(AcFeature::RateSelect2) && !caps.supports(AcFeature::RateSelect5))
        return value==50 || value==75 || value==100;
    return value==1 || value==20 || value==40 || value==60 || value==80 || value==100;
}
unsigned enabled_presets(const ControlPatch& patch) {
    return ((patch.changed&ControlEco) && patch.eco) +
        ((patch.changed&ControlTurbo) && patch.turbo) +
        ((patch.changed&ControlSleep) && patch.sleep) +
        ((patch.changed&ControlFreezeProtection) && patch.freeze_protection);
}
State requested_state(const State& before,const ControlPatch& patch) {
    State s=before;
    if(enabled_presets(patch)) {
        // These are alternative native presets in midea-local. Changing an
        // ordinary field still preserves them; enabling one clears the rest.
        s.eco=s.turbo=s.sleep=s.freeze_protection=false;
        s.comfort_mode=s.power_saving=false;
    }
    if(patch.changed&ControlPower)s.power=patch.power;
    if(patch.changed&ControlMode) {
        s.mode=patch.mode;s.dry=false;
        if(before.mode==3 && patch.mode!=3 && !(patch.changed&ControlFan))s.fan=102;
    }
    if(patch.changed&ControlTemperature)s.setpoint_c=patch.setpoint_c;
    if(patch.changed&ControlFan)s.fan=patch.fan;
    if(patch.changed&ControlSwing)s.swing=patch.swing;
    if(patch.changed&ControlEco)s.eco=patch.eco;
    if(patch.changed&ControlTurbo)s.turbo=patch.turbo;
    if(patch.changed&ControlSleep)s.sleep=patch.sleep;
    if(patch.changed&ControlFreezeProtection) {
        s.freeze_protection=patch.freeze_protection;s.freeze_protection_known=true;
        s.freeze_protection_inferred=false;
    }
    if(patch.changed&ControlAnion)s.purifier=patch.anion;
    return s;
}
bool patch_matches(const State& s,const ControlPatch& patch) {
    if((patch.changed&ControlPower) && s.power!=patch.power)return false;
    if((patch.changed&ControlMode) && s.mode!=patch.mode)return false;
    if((patch.changed&ControlTemperature) && std::fabs(s.setpoint_c-patch.setpoint_c)>0.26)return false;
    if((patch.changed&ControlFan) && s.fan!=patch.fan)return false;
    if((patch.changed&ControlSwing) && s.swing!=patch.swing)return false;
    if((patch.changed&ControlEco) && s.eco!=patch.eco)return false;
    if((patch.changed&ControlTurbo) && s.turbo!=patch.turbo)return false;
    if((patch.changed&ControlSleep) && s.sleep!=patch.sleep)return false;
    if((patch.changed&ControlFreezeProtection) &&
       (!s.freeze_protection_known || s.freeze_protection!=patch.freeze_protection))return false;
    if((patch.changed&ControlAnion) && s.purifier!=patch.anion)return false;
    if((s.properties_known&patch.changed&AcPropertyFields)!=(patch.changed&AcPropertyFields))return false;
    if((patch.changed&ControlDisplay) && s.display!=patch.display)return false;
    if((patch.changed&ControlVerticalAngle) && s.vertical_angle!=patch.vertical_angle)return false;
    if((patch.changed&ControlHorizontalAngle) && s.horizontal_angle!=patch.horizontal_angle)return false;
    if((patch.changed&ControlRateSelect) && s.rate_select!=patch.rate_select)return false;
    if((patch.changed&ControlBreezeless) && s.breezeless!=patch.breezeless)return false;
    if((patch.changed&ControlIndirectWind) && s.indirect_wind!=patch.indirect_wind)return false;
    if((patch.changed&ControlSelfClean) && s.self_clean!=patch.self_clean)return false;
    if((patch.changed&ControlSound) && s.sound!=patch.sound)return false;
    return true;
}
} // namespace

bool valid_ac_control_patch(const State& state,const ControlPatch& patch) {
    if(!patch.changed || (patch.changed&~(AcGeneralFields|AcPropertyFields)) ||
       !state.has_basic || !valid_state(state) ||
       ((patch.changed&AcGeneralFields) && (patch.changed&AcPropertyFields)) ||
       enabled_presets(patch)>1)return false;
    const auto& caps=state.profile.capabilities;
    const uint8_t mode=(patch.changed&ControlMode)?patch.mode:state.mode;
    if((patch.changed&ControlMode) && !mode_supported(caps,patch.mode))return false;
    if(patch.changed&(ControlTemperature|ControlMode)) {
        const double value=(patch.changed&ControlTemperature)?patch.setpoint_c:state.setpoint_c;
        const auto range=temperature_range(caps,mode);
        const double step=temperature_step(caps);
        if(!std::isfinite(value) || value<12 || value>40 || value<range.minimum_c ||
           value>range.maximum_c || std::fabs(value/step-std::round(value/step))>0.00001)
            return false;
    }
    if((patch.changed&ControlFan) && !fan_supported(caps,patch.fan))return false;
    if((patch.changed&ControlMode) && state.mode==3 && patch.mode!=3 &&
       !(patch.changed&ControlFan) && !fan_supported(caps,102))return false;
    if((patch.changed&ControlSwing) && !swing_supported(caps,patch.swing))return false;
    if(state.profile.family==AcProtocol::Subprotocol &&
       (patch.changed&(ControlSwing|ControlFreezeProtection|ControlAnion)))return false;
    if((patch.changed&ControlEco) && !feature_available(state,AcFeature::Eco,ControlEco))return false;
    if((patch.changed&ControlTurbo) && !feature_available(state,
       mode==4?AcFeature::TurboHeat:AcFeature::TurboCool,ControlTurbo))return false;
    if((patch.changed&ControlSleep) && !feature_available(state,AcFeature::Sleep,ControlSleep))return false;
    // An explicit freeze choice supplies the otherwise unknown full-state bit;
    // capability support is not a live on/off observation.
    if((patch.changed&ControlFreezeProtection) && patch.freeze_protection &&
       caps.reports(AcFeature::FreezeProtection) && !caps.supports(AcFeature::FreezeProtection))return false;
    if((patch.changed&ControlAnion) && !feature_available(state,AcFeature::Anion,ControlAnion))return false;
    struct Property { uint32_t field;AcFeature feature; };
    constexpr Property properties[]{
        {ControlDisplay,AcFeature::Display},{ControlVerticalAngle,AcFeature::VerticalAngle},
        {ControlHorizontalAngle,AcFeature::HorizontalAngle},{ControlRateSelect,AcFeature::RateSelect},
        {ControlBreezeless,AcFeature::Breezeless},{ControlIndirectWind,AcFeature::IndirectWind},
        {ControlSelfClean,AcFeature::SelfClean},{ControlSound,AcFeature::Sound}
    };
    for(const auto& property:properties)
        if((patch.changed&property.field) && !feature_available(state,property.feature,property.field))
            return false;
    // B5/0224 also describes devices with only the legacy 0x41 display toggle.
    // The absolute B0/0017 command needs its own current property observation.
    if((patch.changed&ControlDisplay) && !(state.properties_known&ControlDisplay))return false;
    if((patch.changed&ControlVerticalAngle) && !angle_value(patch.vertical_angle))return false;
    if((patch.changed&ControlHorizontalAngle) && !angle_value(patch.horizontal_angle))return false;
    if((patch.changed&ControlRateSelect) && !rate_value(caps,patch.rate_select))return false;
    return true;
}

bool ac_set_state_command(const State& observed,const ControlPatch& patch,uint8_t id,Bytes& frame) {
    if(!valid_ac_control_patch(observed,patch))return false;
    if(patch.changed&AcPropertyFields) {
        Bytes body{0xb0,0};
        const auto add=[&](uint32_t field,uint16_t tag,uint8_t value) {
            if(!(patch.changed&field))return;
            ++body[1];body.push_back(static_cast<uint8_t>(tag));
            body.push_back(static_cast<uint8_t>(tag>>8));body.push_back(1);body.push_back(value);
        };
        add(ControlDisplay,0x0017,patch.display?100:0);
        add(ControlVerticalAngle,0x0009,patch.vertical_angle);
        add(ControlHorizontalAngle,0x000a,patch.horizontal_angle);
        add(ControlRateSelect,0x0048,patch.rate_select);
        add(ControlBreezeless,0x0018,patch.breezeless?1:0);
        add(ControlIndirectWind,0x0042,patch.indirect_wind?2:1);
        add(ControlSelfClean,0x0039,patch.self_clean?1:0);
        add(ControlSound,0x022c,patch.sound?1:0);
        frame=classic_command(std::move(body),id,2,observed.profile.protocol_version);
        return !frame.empty();
    }
    State target=requested_state(observed,patch);
    if(target.mode<1 || target.mode>5 || target.fan>102 ||
       !std::isfinite(target.setpoint_c) || target.setpoint_c<12 || target.setpoint_c>40 ||
       std::fabs(target.setpoint_c*2-std::round(target.setpoint_c*2))>0.00001)return false;
    if(target.profile.family==AcProtocol::Subprotocol) {
        // Timer is serialized in BB/20. Require a current BB/11 byte 25 and,
        // for an enabled timer, the independently queried SN8 flag.
        if(!target.bb_timer_known || (target.bb_timer && !target.bb_sn8_known))return false;
        constexpr uint8_t modes[]{0,3,1,2,4,5};
        const uint8_t temperature=static_cast<uint8_t>(target.setpoint_c*2+30);
        Bytes body{
            static_cast<uint8_t>(2|(target.power?1:0)|(target.power&&target.dry?0x10:0)|
                                 (target.turbo?0x20:0)),
            static_cast<uint8_t>(target.aux_heat?0x40:0x80),
            static_cast<uint8_t>(target.sleep?0x80:0),0,
            static_cast<uint8_t>(target.follow_me?0x40:0),
            static_cast<uint8_t>(modes[target.mode]-1),temperature,target.fan,
            0x32,0,0,0,0,0,0,1,1,0,1,
            static_cast<uint8_t>((target.setpoint_c-1)*2+50),0,temperature,0x32,0x66,0,
            static_cast<uint8_t>((target.eco?0x40:0)|(target.bb_sn8&&target.bb_timer?4:0)),
            0,0,0,0,0,0,0,0,0,0,8
        };
        frame=bb_command(0x20,body,2,target.profile.protocol_version);
        return !frame.empty();
    }
    unsupported_frost(target);
    if(target.classic_aux_bit_ambiguous)return false;
    target=classic_command_defaults(target);
    // Keep the previously verified msmart extended-range 0x40 encoding and
    // add the fields described by the HA protocol reference. This avoids
    // wrapping temperatures outside the primary 17..30 C nibble range.
    ControlPatch core;
    core.changed=ControlTemperature;core.setpoint_c=target.setpoint_c;
    Bytes next;
    if(!set_state_command(target,core,id,next))return false;
    next[8]=target.profile.protocol_version;
    next[18]|=target.power_saving?8:0;
    next[19]|=static_cast<uint8_t>((target.dry?4:0)|(target.smart_eye?1:0));
    next[27]|=target.natural_wind?0x40:0;
    next[32]|=target.comfort_mode?1:0;
    next[next.size()-2]=crc8(next.data()+10,next.size()-12);
    next.back()=checksum(next.data()+1,next.size()-2);
    frame.swap(next);
    return true;
}

bool ac_control_confirmed(const State& before,const State& after,const ControlPatch& patch) {
    if(!valid_ac_control_patch(before,patch) || !after.has_basic || !valid_state(after) ||
       before.profile.family!=after.profile.family)return false;
    State actual=after;
    unsupported_frost(actual);
    if(!patch_matches(actual,patch))return false;
    State expected=requested_state(before,patch);
    unsupported_frost(expected);
    if(expected.power!=actual.power || expected.mode!=actual.mode ||
       std::fabs(expected.setpoint_c-actual.setpoint_c)>0.26 ||
       expected.fan!=actual.fan || expected.swing!=actual.swing)return false;
    if(patch.changed&AcPropertyFields)return true;
    if(expected.eco!=actual.eco || expected.turbo!=actual.turbo ||
       expected.sleep!=actual.sleep || expected.purifier!=actual.purifier ||
       expected.aux_heat!=actual.aux_heat ||
       expected.dry!=actual.dry)return false;
    if(before.profile.family==AcProtocol::Subprotocol) {
        return expected.follow_me==actual.follow_me && (!before.bb_timer_known ||
            (after.bb_timer_known && before.bb_timer==after.bb_timer));
    }
    if(expected.classic_aux_bit_ambiguous || actual.classic_aux_bit_ambiguous)return false;
    expected=classic_command_defaults(expected);
    return observed_optional_matches(expected.target_humidity_known,actual.target_humidity_known,
                                     expected.humidity,actual.humidity) &&
        observed_optional_matches(expected.freeze_protection_known,actual.freeze_protection_known,
                                  expected.freeze_protection,actual.freeze_protection) &&
        observed_optional_matches(expected.fahrenheit_known,actual.fahrenheit_known,
                                  expected.fahrenheit,actual.fahrenheit) &&
        observed_optional_matches(expected.follow_me_known,actual.follow_me_known,
                                  expected.follow_me,actual.follow_me) &&
        observed_optional_matches(expected.independent_aux_heat_known,actual.independent_aux_heat_known,
                                  expected.independent_aux_heat,actual.independent_aux_heat) &&
        observed_optional_matches(expected.comfort_mode_known,actual.comfort_mode_known,
                                  expected.comfort_mode,actual.comfort_mode) &&
        expected.power_saving==actual.power_saving && expected.natural_wind==actual.natural_wind &&
        expected.smart_eye==actual.smart_eye;
}
} // namespace enevina::midea
