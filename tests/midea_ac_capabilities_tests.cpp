// Protocol fixtures follow midea-local (George Zhao, MIT) and msmart-ng
// (Tucker Kern, Colin Kuebler, Mac_Zhou, NeoAcheron, MIT). See the complete
// upstream notices retained in pump/midea_ac_capabilities.cpp.
#include "pump/midea_ac_capabilities.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <initializer_list>

using namespace enevina::midea;
using TestBytes = std::vector<uint8_t>;

namespace {
TestBytes hex(const char* text) {
    TestBytes bytes;
    const auto digit = [](char c) -> uint8_t {
        if(c >= '0' && c <= '9')return static_cast<uint8_t>(c-'0');
        assert(c >= 'a' && c <= 'f');
        return static_cast<uint8_t>(c-'a'+10);
    };
    assert(std::strlen(text)%2 == 0);
    while(*text) {
        bytes.push_back(static_cast<uint8_t>((digit(text[0])<<4)|digit(text[1])));
        text += 2;
    }
    return bytes;
}

uint8_t reference_crc(const TestBytes& bytes) {
    // Independent MSB-first CRC division, reflecting each input/output byte.
    const auto reverse=[](uint8_t value) {
        uint8_t result=0;
        for(unsigned bit=0;bit<8;++bit) {
            result=static_cast<uint8_t>((result<<1)|(value&1));
            value>>=1;
        }
        return result;
    };
    unsigned remainder=0;
    for(const auto byte : bytes) {
        remainder^=reverse(byte);
        for(unsigned bit=0;bit<8;++bit) {
            remainder<<=1;
            if(remainder&0x100)remainder^=0x131;
        }
    }
    return reverse(static_cast<uint8_t>(remainder));
}

uint8_t reference_sum(const TestBytes& bytes,size_t begin,size_t end) {
    unsigned sum=0;
    for(size_t i=begin;i<end;++i)sum+=bytes[i];
    return static_cast<uint8_t>(0x100-(sum&0xff));
}

TestBytes response(TestBytes body,bool additive=false,uint8_t message_type=3) {
    body.push_back(additive ? reference_sum(body,0,body.size()) : reference_crc(body));
    TestBytes frame{0xaa,static_cast<uint8_t>(10+body.size()),0xac,0,0,0,0,0,3,message_type};
    frame.insert(frame.end(),body.begin(),body.end());
    frame.push_back(reference_sum(frame,1,frame.size()));
    return frame;
}

TestBytes scalar_response(uint16_t tag,uint8_t value) {
    return response({0xb5,1,static_cast<uint8_t>(tag),static_cast<uint8_t>(tag>>8),1,value});
}

AcCapabilities decode(const TestBytes& frame) {
    AcCapabilities caps;
    assert(parse_capabilities(frame.data(),frame.size(),caps));
    assert(caps.received);
    return caps;
}

void expect_flag(const AcCapabilities& caps,AcFeature feature,bool expected) {
    assert(caps.reports(feature));
    assert(caps.supports(feature) == expected);
    assert(caps.supports(feature,true) == expected);
}

bool same_capabilities(const AcCapabilities& a,const AcCapabilities& b) {
    if(a.known!=b.known || a.supported!=b.supported || a.received!=b.received)return false;
    for(size_t i=0;i<a.temperatures.size();++i) {
        if(a.temperatures[i].known!=b.temperatures[i].known ||
           a.temperatures[i].minimum_c!=b.temperatures[i].minimum_c ||
           a.temperatures[i].maximum_c!=b.temperatures[i].maximum_c)return false;
    }
    return true;
}

void query_vectors() {
    // Independently serialized with the pinned Python reference CRC table.
    assert(capabilities_query(1) == hex("aa0fac00000000000003b5010001e5a6"));
    assert(capabilities_query(2,true) == hex("aa10ac00000000000003b501010102c1c6"));
    assert(capabilities_query(255,false,8) == hex("aa0fac00000000000803b50100ff8ef7"));
    assert(capabilities_query(0,true,12) == hex("aa10ac00000000000c03b5010101007d00"));
}

void absent_capabilities_keep_base_controls() {
    const AcCapabilities caps{};
    assert(!caps.received && caps.known == 0 && caps.supported == 0);
    assert(!caps.reports(AcFeature::HeatMode));
    assert(!caps.supports(AcFeature::HeatMode));
    assert(caps.supports(AcFeature::HeatMode,true));
    for(uint8_t mode=1; mode<=5; ++mode)assert(mode_supported(caps,mode));
    assert(!mode_supported(caps,0) && !mode_supported(caps,6));
    for(const uint8_t fan : {40,60,80,100,102})assert(fan_supported(caps,fan));
    assert(!fan_supported(caps,20));
    assert(!fan_supported(caps,73));
    assert(!fan_supported(caps,101) && !fan_supported(caps,103));
    for(const uint8_t swing : {0,3,12,15})assert(swing_supported(caps,swing));
    assert(!swing_supported(caps,1) && !swing_supported(caps,16));
    assert(temperature_step(caps) == 1.0);
    for(uint8_t mode=0;mode<8;++mode) {
        const auto range=temperature_range(caps,mode);
        assert(!range.known && range.minimum_c == 16.0 && range.maximum_c == 30.0);
    }
    // Invalid enum values cannot shift a uint64_t out of range.
    assert(!caps.reports(static_cast<AcFeature>(255)));
    assert(!caps.supports(static_cast<AcFeature>(255),true));
    for(unsigned index=0;index<=static_cast<unsigned>(AcFeature::FreshAir);++index) {
        const auto feature=static_cast<AcFeature>(index);
        AcCapabilities explicit_caps;
        explicit_caps.known=uint64_t{1}<<index;
        assert(explicit_caps.reports(feature));
        assert(!explicit_caps.supports(feature,true));
        explicit_caps.supported=explicit_caps.known;
        assert(explicit_caps.supports(feature));
    }
}

void mode_value_semantics() {
    // Truth table from the pinned reference, ordered AUTO/COOL/DRY/HEAT.
    const bool modes[16][4]={
        {true,true,true,false}, {true,true,true,true},
        {true,false,false,true}, {false,true,false,false},
        {false,true,false,true}, {false,true,true,false},
        {false,true,true,true}, {true,true,false,true},
        {true,true,false,false}, {true,true,true,true},
        {false,false,false,true}, {false,true,true,true},
        {false,false,false,true}, {true,true,true,true},
        {false,true,false,false}, {false,true,false,false}
    };
    const AcFeature features[]={AcFeature::AutoMode,AcFeature::CoolMode,
                                AcFeature::DryMode,AcFeature::HeatMode};
    for(unsigned raw=0;raw<256;++raw) {
        const auto caps=decode(scalar_response(0x0214,static_cast<uint8_t>(raw)));
        for(unsigned i=0;i<4;++i) {
            const bool expected=raw<16 ? modes[raw][i] : i==1;
            expect_flag(caps,features[i],expected);
            assert(mode_supported(caps,static_cast<uint8_t>(i+1)) == expected);
        }
        assert(mode_supported(caps,5));
        assert(!caps.reports(AcFeature::FreezeProtection));
        assert(!caps.reports(AcFeature::AuxHeat));
    }
}

void fan_value_semantics() {
    // SILENT/LOW/MEDIUM/HIGH/AUTO/CUSTOM. Profile 1 means all named speeds too.
    const bool fans[10][6]={
        {false,false,false,false,false,false},
        {true,true,true,true,true,true},
        {false,false,false,false,false,false},
        {false,true,false,true,false,false},
        {false,true,false,true,true,false},
        {false,true,true,true,true,false},
        {true,true,true,true,true,false},
        {false,true,true,true,false,false},
        {false,false,false,false,false,false},
        {true,true,false,true,true,false}
    };
    const AcFeature features[]={AcFeature::FanSilent,AcFeature::FanLow,
        AcFeature::FanMedium,AcFeature::FanHigh,AcFeature::FanAuto,AcFeature::FanCustom};
    const uint8_t speeds[]={20,40,60,80,102};
    for(unsigned raw=0;raw<256;++raw) {
        const auto caps=decode(scalar_response(0x0210,static_cast<uint8_t>(raw)));
        for(unsigned i=0;i<6;++i) {
            const bool expected=raw<10 && fans[raw][i];
            expect_flag(caps,features[i],expected);
            if(i<5)assert(fan_supported(caps,speeds[i]) == expected);
        }
        assert(fan_supported(caps,100) == (raw<10 && fans[raw][3]));
        for(const uint8_t speed : {0,1,19,21,39,41,59,61,79,81,99})
            assert(fan_supported(caps,speed) == (raw==1));
        assert(!fan_supported(caps,101) && !fan_supported(caps,255));
    }
}

void swing_value_semantics() {
    // Vertical only, both, neither, horizontal only.
    const bool vertical[]={true,true,false,false};
    const bool horizontal[]={false,true,false,true};
    for(unsigned raw=0;raw<256;++raw) {
        const auto caps=decode(scalar_response(0x0215,static_cast<uint8_t>(raw)));
        const bool v=raw<4 && vertical[raw],h=raw<4 && horizontal[raw];
        expect_flag(caps,AcFeature::SwingVertical,v);
        expect_flag(caps,AcFeature::SwingHorizontal,h);
        assert(swing_supported(caps,0));
        assert(swing_supported(caps,3) == h);
        assert(swing_supported(caps,12) == v);
        assert(swing_supported(caps,15) == (v && h));
        for(const uint8_t invalid : {1,2,4,8,16,48,255})assert(!swing_supported(caps,invalid));
    }
}

void optional_feature_semantics() {
    struct BooleanTag { uint16_t tag; AcFeature feature; };
    const BooleanTag tags[]={
        {0x0213,AcFeature::FreezeProtection}, {0x0219,AcFeature::AuxHeat},
        {0x021e,AcFeature::Anion}, {0x022c,AcFeature::Sound},
        {0x0009,AcFeature::VerticalAngle}, {0x000a,AcFeature::HorizontalAngle},
        {0x0039,AcFeature::SelfClean}, {0x0018,AcFeature::Breezeless},
        {0x0042,AcFeature::IndirectWind}, {0x004b,AcFeature::FreshAir}
    };
    for(unsigned raw=0;raw<256;++raw) {
        const auto value=static_cast<uint8_t>(raw);
        for(const auto& tag : tags) {
            const auto caps=decode(scalar_response(tag.tag,value));
            expect_flag(caps,tag.feature,raw==1);
            assert(!caps.reports(AcFeature::Sleep)); // No support tag for ordinary sleep.
        }
        const auto eco=decode(scalar_response(0x0212,value));
        expect_flag(eco,AcFeature::Eco,raw==1 || raw==2);
        const auto turbo=decode(scalar_response(0x021a,value));
        expect_flag(turbo,AcFeature::TurboCool,raw<2);
        expect_flag(turbo,AcFeature::TurboHeat,raw==1 || raw==3);
        const auto display=decode(scalar_response(0x0224,value));
        expect_flag(display,AcFeature::Display,raw==1 || raw==2 || raw==100);
        const auto energy=decode(scalar_response(0x0216,value));
        expect_flag(energy,AcFeature::EnergyStats,raw>=2 && raw<=5);
        expect_flag(energy,AcFeature::EnergySetting,raw==3 || raw==5);
        expect_flag(energy,AcFeature::EnergyBcd,raw==2 || raw==3);
        assert(!energy.reports(AcFeature::RateSelect));
        const auto rate=decode(scalar_response(0x0048,value));
        expect_flag(rate,AcFeature::RateSelect,raw>0);
        expect_flag(rate,AcFeature::RateSelect2,(raw%2)==1);
        expect_flag(rate,AcFeature::RateSelect5,(raw%4)>=2);
        assert(!rate.reports(AcFeature::EnergyStats));
        const auto humidity=decode(scalar_response(0x021f,value));
        expect_flag(humidity,AcFeature::Humidity,(raw%4)!=0);
    }
}

void temperature_ranges_and_steps() {
    for(const uint8_t flag : {0,1,5,255}) {
        const auto caps=decode(response({0xb5,1,0x25,2,7,33,61,34,62,16,64,flag}));
        expect_flag(caps,AcFeature::HalfDegree,flag!=0);
        assert(temperature_step(caps) == (flag ? 0.5 : 1.0));
        const double expected[6][2]={{16,30},{17,31},{16.5,30.5},{16.5,30.5},{8,32},{16.5,30.5}};
        for(uint8_t mode=0;mode<6;++mode) {
            const auto range=temperature_range(caps,mode);
            assert(range.known == (mode!=0));
            assert(range.minimum_c == expected[mode][0]);
            assert(range.maximum_c == expected[mode][1]);
        }
        assert(!caps.reports(AcFeature::FreezeProtection)); // A heat minimum is not a frost preset.
    }
    // Older six-byte range tags omit the step flag; never fabricate its value.
    auto caps=decode(response({0xb5,1,0x25,2,6,32,60,34,62,20,64}));
    assert(temperature_range(caps,4).minimum_c == 10);
    assert(!caps.reports(AcFeature::HalfDegree) && temperature_step(caps) == 1.0);
    // Nonempty future extensions preserve the documented seven bytes.
    caps=decode(response({0xb5,1,0x25,2,8,32,60,34,62,20,64,1,0x55}));
    assert(temperature_step(caps) == 0.5 && temperature_range(caps,1).minimum_c == 17);
}

void upstream_pages_merge() {
    // Real upstream msmart-ng response fixtures, including their checksums.
    const auto first=hex("aa3dac00000000000303b50a12020101430001011402010115020101160201001a020101100201011f020103250207203c203c203c05400001000100c805");
    const auto second=hex("aa23ac00000000000303b5051e020101130201012202010019020100390001010000febe");
    auto caps=decode(first);
    expect_flag(caps,AcFeature::Eco,true);
    expect_flag(caps,AcFeature::FanCustom,true);
    expect_flag(caps,AcFeature::DryMode,true);
    expect_flag(caps,AcFeature::EnergyStats,false);
    assert(!caps.reports(AcFeature::FreezeProtection));
    assert(parse_capabilities(second.data(),second.size(),caps));
    expect_flag(caps,AcFeature::FreezeProtection,true);
    expect_flag(caps,AcFeature::AuxHeat,false);
    expect_flag(caps,AcFeature::SelfClean,true);
    expect_flag(caps,AcFeature::Anion,true);
    expect_flag(caps,AcFeature::Eco,true);
    expect_flag(caps,AcFeature::FanCustom,true);
    expect_flag(caps,AcFeature::DryMode,true);
    expect_flag(caps,AcFeature::EnergyStats,false);
    assert(temperature_step(caps)==0.5 && temperature_range(caps,4).minimum_c==16);
    const auto empty_page=response({0xb5,0});
    const auto saved=caps;
    assert(parse_capabilities(empty_page.data(),empty_page.size(),caps));
    assert(same_capabilities(saved,caps));
    // A later explicit false overrides the earlier flag, without erasing others.
    const auto no_eco=scalar_response(0x0212,0);
    assert(parse_capabilities(no_eco.data(),no_eco.size(),caps));
    expect_flag(caps,AcFeature::Eco,false);
    expect_flag(caps,AcFeature::FreezeProtection,true);
    // Known upstream six-byte trailer padding is not another capability.
    caps=decode(hex("aa3bac00000000000303b50a1e02010113020101220201001902010039000101580001024200010159000101090001010a000101000000000000cfbf"));
    expect_flag(caps,AcFeature::IndirectWind,true);
    expect_flag(caps,AcFeature::HorizontalAngle,true);
    assert(!caps.reports(AcFeature::Sleep));
}

void unknown_empty_and_duplicate_tags() {
    auto caps=decode(response({0xb5,4,0x12,2,1,1,0xfe,0xff,3,0,1,2,
                               0x12,2,0,0x12,2,1,0}));
    expect_flag(caps,AcFeature::Eco,false); // Last nonempty duplicate wins.
    assert(caps.known == (uint64_t{1}<<static_cast<unsigned>(AcFeature::Eco)));
    caps=decode(response({0xb5,2,0x13,2,0,0xfe,0xff,0}));
    assert(caps.known==0 && caps.supported==0);
    caps=decode(response({0xb5,1,0x12,2,2,2,0xaa}));
    expect_flag(caps,AcFeature::Eco,true); // Extra scalar bytes are extensions.
    caps=decode(response({0xb5,1,0x13,2,1,0},true));
    expect_flag(caps,AcFeature::FreezeProtection,false); // Additive payload checksum variant.
}

void malformed_responses_are_atomic() {
    const auto initial=decode(response({0xb5,2,0x12,2,1,1,0x25,2,7,32,60,34,62,20,64,1}));
    const auto rejected=[&](const TestBytes& frame) {
        auto caps=initial;
        assert(!parse_capabilities(frame.data(),frame.size(),caps));
        assert(same_capabilities(caps,initial));
    };
    auto caps=initial;
    assert(!parse_capabilities(nullptr,0,caps));
    assert(!parse_capabilities(nullptr,256,caps));
    assert(same_capabilities(caps,initial));
    const auto valid=scalar_response(0x0214,7);
    for(size_t length=0;length<valid.size();++length)
        rejected(TestBytes(valid.begin(),valid.begin()+length));
    rejected(TestBytes(257,0));
    for(const size_t field : {size_t{0},size_t{1},size_t{2}}) {
        auto bad=valid;
        bad[field]^=1;
        bad.back()=reference_sum(bad,1,bad.size()-1);
        rejected(bad);
    }
    for(const uint8_t type : {0,2,4,5,6,10,255})
        rejected(response({0xb5,1,0x12,2,1,1},false,type));
    for(const uint8_t body_type : {0x40,0xb0,0xb1,0xc0})
        rejected(response({body_type,1,0x12,2,1,1}));
    auto bad=valid;
    bad.back()^=1;
    rejected(bad);
    bad=valid;
    bad[bad.size()-2]^=0x20;
    bad.back()=reference_sum(bad,1,bad.size()-1);
    rejected(bad); // Valid outer checksum cannot hide a broken payload CRC.
    rejected(response({0xb5}));
    rejected(response({0xb5,1}));
    rejected(response({0xb5,1,0x14}));
    rejected(response({0xb5,1,0x14,2}));
    rejected(response({0xb5,1,0x14,2,1}));
    rejected(response({0xb5,1,0x14,2,255,1}));
    rejected(response({0xb5,2,0x14,2,1,0}));
    // A complete first tag must not leak when a later tag is truncated.
    rejected(response({0xb5,2,0x12,2,1,0,0x25,2,7,32,60}));
    for(uint8_t length=1;length<6;++length) {
        TestBytes body{0xb5,1,0x25,2,length};
        body.insert(body.end(),length,32);
        rejected(response(body));
    }
    rejected(response({0xb5,1,0x25,2,7,62,32,34,62,20,64,1}));
    rejected(response({0xb5,1,0x25,2,7,32,255,34,62,20,64,1}));
}
}

int main(int argc,char** argv) {
    const auto selected=[&](const char* name) {
        return argc == 1 || std::strcmp(argv[1],name) == 0;
    };
    if(selected("queries"))query_vectors();
    if(selected("defaults"))absent_capabilities_keep_base_controls();
    if(selected("modes"))mode_value_semantics();
    if(selected("fans"))fan_value_semantics();
    if(selected("swings"))swing_value_semantics();
    if(selected("features"))optional_feature_semantics();
    if(selected("temperatures"))temperature_ranges_and_steps();
    if(selected("merge"))upstream_pages_merge();
    if(selected("tags"))unknown_empty_and_duplicate_tags();
    if(selected("malformed"))malformed_responses_are_atomic();
    std::cout << "Midea AC B5 capability tests PASS\n";
}
