#include "pump/daikin_dsiot.hpp"
#include <cassert>

using namespace enevina;
using namespace enevina::dsiot;

int main() {
    const auto request=status_request();
    assert(request.find(indoor_path)!=request.npos && request.find(identity_path)!=request.npos);
    const std::string sample=R"(
{"responses":[
  {"rsc":2000,"fr":"/dsiot/edge.adp_i","pc":{"pn":"adp_i","pch":[
    {"pn":"mac","pv":"38A2C3912427"},{"pn":"ver","pv":"3_12_3"},{"pn":"api_ver","pv":"2_2"}]}},
  {"rsc":2000,"fr":"/dsiot/edge/adr_0100.dgc_status","pc":{"pn":"dgc_status","pch":[
    {"pn":"e_1002","pch":[
      {"pn":"e_A002","pch":[{"pn":"p_01","pv":"01"}]},
      {"pn":"e_3001","pch":[{"pn":"p_01","pv":"0100"},{"pn":"p_03","pv":"2B"}]},
      {"pn":"e_A00B","pch":[{"pn":"p_01","pv":"15"}]},
      {"pn":"e_A001","pch":[{"pn":"p_01","pv":"204654584d3731"}]}]}]}},
  {"rsc":2000,"fr":"/dsiot/edge/adr_0200.dgc_status","pc":{"pn":"dgc_status","pch":[
    {"pn":"e_1003","pch":[{"pn":"e_A00D","pch":[{"pn":"p_01","pv":"fb"}]}]}]}}
]})";
    const auto state=decode_status(sample);
    assert(state && state->device.mac=="38a2c3912427");
    assert(state->device.firmware=="3_12_3" && state->device.api_version=="2_2");
    assert(state->device.model=="FTXM71");
    assert(state->power && state->mode==Mode::Heat && heating_mode(*state)==PumpRoomMode::Heat);
    assert(state->target_c==21.5 && state->indoor_c==21 && state->outdoor_c==-2.5);
    assert(!decode_status(sample+"{}"));
    auto missing=sample;
    const size_t indoor=missing.find("\"pv\":\"15\"");
    missing.replace(indoor,9,"\"pv\":\"--\"");
    const auto unknown=decode_status(missing);
    assert(unknown && !unknown->indoor_c);
    auto wrong=sample;
    wrong.replace(wrong.find("\"pv\":\"01\""),9,"\"pv\":\"02\"");
    assert(!decode_status(wrong));

    const PumpHeatingLimits limits{16,30,.5,true,true};
    const auto target=write_request(*state,{{},22.5,false},limits);
    assert(target && target->find("\"pn\":\"p_03\",\"pv\":\"2D\"")!=target->npos);
    assert(target->find("e_A002")==target->npos);
    const auto off=write_request(*state,{false,{},false},limits);
    assert(off && off->find("e_A002")!=off->npos && off->find("e_3001")==off->npos);
    const auto heat=write_request(*state,{true,22.5,true},limits);
    assert(heat && heat->find("e_A002")!=heat->npos && heat->find("0100")!=heat->npos);
    assert(!write_request(*state,{{},22.1,false},limits));
    assert(!write_request(*state,{false,22,false},limits));
    assert(!readback_matches(*state,*state,{{},22.5,false}));
    auto after=*state;
    after.target_c=22.5;
    assert(readback_matches(*state,after,{{},22.5,false}));
    after.device.mac="001122334455";
    assert(!readback_matches(*state,after,{{},22.5,false}));
    assert(write_accepted(R"({"responses":[{"fr":"/dsiot/edge/adr_0100.dgc_status","rsc":2004}]})"));
    assert(!write_accepted(R"({"responses":[{"fr":"/dsiot/edge/adr_0200.dgc_status","rsc":2004}]})"));
}
