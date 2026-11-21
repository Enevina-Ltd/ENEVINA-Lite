// Native C++ adaptation of capability semantics from midea-local 15.2.1,
// third_party/midea-local/midealocal/devices/ac/message.py (commit 425bba4),
// and msmart-ng msmart/device/AC/command.py for additional capability IDs.
// These upstream portions retain the following MIT terms.
//
// MIT License
// Copyright (c) 2021 George Zhao
// Copyright (c) 2022-2026 Tucker Kern
// Copyright (c) 2020 Colin Kuebler (@kueblc)
// Copyright (c) 2020 Mac_Zhou (@mac-zhou)
// Copyright (c) 2018 NeoAcheron
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include "midea_ac_capabilities.hpp"
#include <initializer_list>

namespace enevina::midea {
namespace {
uint64_t feature_bit(AcFeature feature) {
    const auto index=static_cast<uint8_t>(feature);
    return index<=static_cast<uint8_t>(AcFeature::FreshAir) ? uint64_t{1}<<index : 0;
}
uint8_t capability_crc8(const uint8_t* bytes,size_t size) {
    uint8_t crc=0;
    for(size_t i=0;i<size;++i) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;++bit)
            crc=static_cast<uint8_t>((crc&1) ? (crc>>1)^0x8c : crc>>1);
    }
    return crc;
}
uint8_t capability_checksum(const uint8_t* bytes,size_t size) {
    uint8_t sum=0;
    for(size_t i=0;i<size;++i)sum=static_cast<uint8_t>(sum+bytes[i]);
    return static_cast<uint8_t>(-sum);
}
bool is_one_of(uint8_t value,std::initializer_list<uint8_t> alternatives) {
    for(const auto alternative : alternatives)if(value==alternative)return true;
    return false;
}
void report_feature(AcCapabilities& caps,AcFeature feature,bool supported) {
    const uint64_t bit=feature_bit(feature);
    caps.known|=bit;
    if(supported)caps.supported|=bit;
    else caps.supported&=~bit;
}
bool read_capability(uint16_t tag,const uint8_t* value,size_t length,
                     AcCapabilities& caps) {
    // Empty values are omissions, not explicit unsupported flags.
    if(!length)return true;
    const uint8_t raw=value[0];
    switch(tag) {
        case 0x0214: // Mode enumeration; raw zero still supports cool/dry/auto.
            report_feature(caps,AcFeature::HeatMode,
                           is_one_of(raw,{1,2,4,6,7,9,10,11,12,13}));
            report_feature(caps,AcFeature::CoolMode,!is_one_of(raw,{2,10,12}));
            report_feature(caps,AcFeature::DryMode,is_one_of(raw,{0,1,5,6,9,11,13}));
            report_feature(caps,AcFeature::AutoMode,is_one_of(raw,{0,1,2,7,8,9,13}));
            break;
        case 0x0215:
            report_feature(caps,AcFeature::SwingHorizontal,is_one_of(raw,{1,3}));
            report_feature(caps,AcFeature::SwingVertical,raw<2);
            break;
        case 0x0210: {
            const bool custom=raw==1;
            report_feature(caps,AcFeature::FanSilent,custom || is_one_of(raw,{6,9}));
            report_feature(caps,AcFeature::FanLow,custom || is_one_of(raw,{3,4,5,6,7,9}));
            report_feature(caps,AcFeature::FanMedium,custom || is_one_of(raw,{5,6,7}));
            report_feature(caps,AcFeature::FanHigh,custom || is_one_of(raw,{3,4,5,6,7,9}));
            report_feature(caps,AcFeature::FanAuto,custom || is_one_of(raw,{4,5,6,9}));
            report_feature(caps,AcFeature::FanCustom,custom);
            break;
        }
        case 0x0212:report_feature(caps,AcFeature::Eco,is_one_of(raw,{1,2}));break;
        case 0x0213:report_feature(caps,AcFeature::FreezeProtection,raw==1);break;
        case 0x0219:report_feature(caps,AcFeature::AuxHeat,raw==1);break;
        case 0x021e:report_feature(caps,AcFeature::Anion,raw==1);break;
        case 0x021a:
            report_feature(caps,AcFeature::TurboCool,raw<2);
            report_feature(caps,AcFeature::TurboHeat,is_one_of(raw,{1,3}));
            break;
        case 0x0224:report_feature(caps,AcFeature::Display,is_one_of(raw,{1,2,100}));break;
        case 0x0009:report_feature(caps,AcFeature::VerticalAngle,raw==1);break;
        case 0x000a:report_feature(caps,AcFeature::HorizontalAngle,raw==1);break;
        case 0x0039:report_feature(caps,AcFeature::SelfClean,raw==1);break;
        case 0x0018:report_feature(caps,AcFeature::Breezeless,raw==1);break;
        case 0x0042:report_feature(caps,AcFeature::IndirectWind,raw==1);break;
        case 0x004b:report_feature(caps,AcFeature::FreshAir,raw==1);break;
        case 0x022c:report_feature(caps,AcFeature::Sound,raw==1);break;
        case 0x021f:report_feature(caps,AcFeature::Humidity,(raw&3)!=0);break;
        case 0x0216:
            report_feature(caps,AcFeature::EnergyStats,is_one_of(raw,{2,3,4,5}));
            report_feature(caps,AcFeature::EnergySetting,is_one_of(raw,{3,5}));
            report_feature(caps,AcFeature::EnergyBcd,is_one_of(raw,{2,3}));
            break;
        case 0x0048:
            report_feature(caps,AcFeature::RateSelect,raw>0);
            report_feature(caps,AcFeature::RateSelect2,(raw&1)!=0);
            report_feature(caps,AcFeature::RateSelect5,(raw&2)!=0);
            break;
        case 0x0225: {
            // Old six-byte responses omit the resolution flag. Preserve its
            // prior knowledge rather than treating a missing byte as false.
            if(length<6)return false;
            for(size_t i=0;i<6;i+=2)
                if(value[i]==0xff || value[i+1]==0xff || value[i]>value[i+1])return false;
            const AcTemperatureRange cool{true,value[0]*0.5,value[1]*0.5};
            const AcTemperatureRange automatic{true,value[2]*0.5,value[3]*0.5};
            const AcTemperatureRange heat{true,value[4]*0.5,value[5]*0.5};
            caps.temperatures[1]=automatic;
            caps.temperatures[2]=cool;
            caps.temperatures[3]=cool;
            caps.temperatures[4]=heat;
            caps.temperatures[5]=cool;
            if(length>=7)report_feature(caps,AcFeature::HalfDegree,value[6]!=0);
            break;
        }
        default:break; // Well-formed unsupported tags are safe to skip.
    }
    return true;
}
}

bool AcCapabilities::reports(AcFeature feature) const {
    return (known&feature_bit(feature)) != 0;
}
bool AcCapabilities::supports(AcFeature feature,bool fallback) const {
    const uint64_t bit=feature_bit(feature);
    return bit && ((known&bit) ? (supported&bit)!=0 : fallback);
}
std::vector<uint8_t> capabilities_query(uint8_t message_id,bool additional,
                                       uint8_t protocol_version) {
    std::vector<uint8_t> frame{0xaa,0,0xac,0,0,0,0,0,protocol_version,0x03,
                               0xb5,0x01,static_cast<uint8_t>(additional ? 1 : 0)};
    if(additional)frame.push_back(0x01);
    frame.push_back(message_id);
    frame.push_back(capability_crc8(frame.data()+10,frame.size()-10));
    frame[1]=static_cast<uint8_t>(frame.size());
    frame.push_back(capability_checksum(frame.data()+1,frame.size()-1));
    return frame;
}
bool parse_capabilities(const uint8_t* frame,size_t n,AcCapabilities& out) {
    // 10-byte header + B5/count + payload CRC + frame checksum. A notify2 B5
    // may carry live extended state, so only QUERY responses are capabilities.
    if(!frame || n<14 || n>256 || frame[0]!=0xaa || frame[2]!=0xac ||
       static_cast<size_t>(frame[1])+1!=n || frame[9]!=0x03 || frame[10]!=0xb5 ||
       capability_checksum(frame+1,n-2)!=frame[n-1])return false;
    const uint8_t* payload=frame+10;
    const size_t payload_length=n-12; // Exclude both checksums, retain metadata.
    if(payload[payload_length]!=capability_crc8(payload,payload_length) &&
       payload[payload_length]!=capability_checksum(payload,payload_length))return false;

    AcCapabilities candidate=out;
    size_t position=2;
    for(unsigned index=0;index<payload[1];++index) {
        if(payload_length-position<3)return false;
        const uint16_t tag=static_cast<uint16_t>(payload[position]) |
                           static_cast<uint16_t>(payload[position+1]<<8);
        const size_t length=payload[position+2];
        position+=3;
        if(length>payload_length-position)return false;
        if(!read_capability(tag,payload+position,length,candidate))return false;
        position+=length;
    }
    // Real B5 responses use differing page/serial/reserved trailers, including
    // 0, 2 and 6 bytes. The declared count bounds capability parsing; trailers
    // are included in CRC verification and cannot create support flags.
    candidate.received=true;
    out=candidate;
    return true;
}
double temperature_step(const AcCapabilities& caps) {
    return caps.supports(AcFeature::HalfDegree) ? 0.5 : 1.0;
}
AcTemperatureRange temperature_range(const AcCapabilities& caps,uint8_t mode) {
    return mode<caps.temperatures.size() ? caps.temperatures[mode] : AcTemperatureRange{};
}
bool mode_supported(const AcCapabilities& caps,uint8_t mode) {
    switch(mode) {
        case 1:return caps.supports(AcFeature::AutoMode,true);
        case 2:return caps.supports(AcFeature::CoolMode,true);
        case 3:return caps.supports(AcFeature::DryMode,true);
        case 4:return caps.supports(AcFeature::HeatMode,true);
        case 5:return true; // Fan-only has no B5 support flag.
        default:return false;
    }
}
bool fan_supported(const AcCapabilities& caps,uint8_t speed) {
    switch(speed) {
        case 20:return caps.supports(AcFeature::FanSilent,caps.supports(AcFeature::FanCustom));
        case 40:return caps.supports(AcFeature::FanLow,true);
        case 60:return caps.supports(AcFeature::FanMedium,true);
        case 80:
        case 100:return caps.supports(AcFeature::FanHigh,true);
        case 102:return caps.supports(AcFeature::FanAuto,true);
        default:return speed<=100 && caps.supports(AcFeature::FanCustom);
    }
}
bool swing_supported(const AcCapabilities& caps,uint8_t swing) {
    switch(swing) {
        case 0:return true;
        case 3:return caps.supports(AcFeature::SwingHorizontal,true);
        case 12:return caps.supports(AcFeature::SwingVertical,true);
        case 15:return caps.supports(AcFeature::SwingHorizontal,true) &&
                       caps.supports(AcFeature::SwingVertical,true);
        default:return false;
    }
}
} // namespace enevina::midea
