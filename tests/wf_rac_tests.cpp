#include "pump/wf_rac.hpp"
#include <cassert>

using namespace enevina;
using namespace enevina::wf_rac;

namespace {
std::string bytes(std::string_view hex) {
    std::string out;
    for(size_t i=0;i<hex.size();i+=2)
        out.push_back(static_cast<char>(std::stoi(std::string(hex.substr(i,2)),nullptr,16)));
    return out;
}
}

int main() {
    assert(crc16("123456789")==0x29b1);
    // Independent fixtures generated with Python binascii.crc_hqx.
    const auto standard=bytes(
        "0000000000ff00000000000000000000000001ffffffffd2ef"
        "830411002d900000000200000000000000000380209000801070009410000066eb");
    auto legacy=standard;
    legacy[legacy.size()-2]=static_cast<char>(0xf6);
    legacy.back()=static_cast<char>(0x62);
    for(const auto& frame:{standard,legacy}) {
        const auto state=decode(frame);
        assert(state && state->model_number==3 && state->power && state->mode==Mode::Heat);
        assert(state->target_c==22.5 && state->compressor_demand);
        assert(state->indoor_c==21.2);
        assert(state->outdoor_c.has_value());
    }
    auto corrupted=legacy;
    corrupted[29]^=1;
    assert(!decode(corrupted));
    corrupted=legacy;
    corrupted[18]=static_cast<char>(0xff);
    assert(!decode(corrupted));
    corrupted=legacy;
    corrupted[10]^=1;
    assert(!decode(corrupted));
    assert(!decode(std::string_view(legacy).substr(0,legacy.size()-1)));
    assert(!decode(legacy+"x"));

    assert(unit_id("34:8E:89:C5:A1:37")=="348e89c5a137");
    assert(unit_id("34-8E-89-C5-A1-37")=="348e89c5a137");
    assert(!unit_id("34:8E89:C5:A1:37") && !unit_id("000000000000"));
    assert(!unit_id("348e89c5a137/path"));
    assert(operator_id_valid("f582452f-8d4a-4412-a464-5d758dd4aac7"));
    assert(!operator_id_valid("f582452f-8d4a-4412-a464-5d758dd4aag7"));
    assert(!operator_id_valid("aws") && !operator_id_valid(""));

    const PumpHeatingLimits limits{18,30,.5,true,true};
    const auto off=command({false,{},false},limits);
    const auto heat=command({true,22.5,true},limits);
    assert(off && *off==bytes(
        "0000020000ff00000000000000000000000001ffffffff1f54"
        "0000000000ff00000000000000000000000001ffffffffd2ef"));
    assert(heat && *heat==bytes(
        "00003300adff00000000000000000000000001ffffffffd2f6"
        "0000000000ff00000000000000000000000001ffffffffd2ef"));
    assert(!command({},limits));
    assert(!command({false,22.5,false},limits));
    assert(!command({true,21.2,true},limits));
    assert(!command({true,17.5,true},limits));

    const auto state=decode(legacy);
    auto before=*state,after=*state;
    before.target_c=21.;
    assert(!readback_matches(before,before,{{},22.5,false}));
    assert(readback_matches(before,after,{{},22.5,false}));
    after.mode=Mode::Cooling;
    assert(!readback_matches(before,after,{{},22.5,false}));
    assert(!readback_matches(before,after,{true,22.5,true}));
    after.power=false;
    assert(!readback_matches(before,after,{false,{},false}));
    assert(heating_mode(*state)==PumpRoomMode::Heat);
    assert(same_request(*state,*state) && !same_request(*state,after));
    assert(permits_heat(*state,{{},23.0,false}));
    assert(!permits_heat(after,{{},23.0,false}));
    assert(changes_mode(*state,{false,{},false}) && !changes_mode(*state,{{},22.5,false}));
    after=*state;
    after.fault=0x80;
    assert(!permits_heat(after,{false,{},false}));
    after=*state;
    after.self_clean=true;
    assert(!permits_heat(after,{false,{},false}));
    after.power=false;
    assert(heating_mode(after)==PumpRoomMode::Off);
}
