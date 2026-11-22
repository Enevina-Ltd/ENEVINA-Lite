#include "pump/daikin_legacy.hpp"
#include <cassert>
#include <string>

using namespace enevina::daikin;

int main() {
    const auto unit=decode_identity("ret=OK,type=aircon,ver=1_14_72,mac=0A1B2C3D4E5F,model=BRP069B41");
    assert(unit && unit->mac=="0a1b2c3d4e5f");
    assert(unit->firmware=="1_14_72" && unit->model=="BRP069B41");
    assert(!decode_identity("ret=OK,type=airbase,mac=0A1B2C3D4E5F"));
    assert(!decode_identity("ret=OK,type=aircon,mac=000000000000"));
    assert(!decode_identity("ret=OK,ret=PARAM NG,type=aircon,mac=0A1B2C3D4E5F"));

    const auto dry=decode_state("ret=OK,pow=1,mode=2,stemp=M,shum=50,dh4=0,dfr4=A,dfd4=1,f_rate=5,f_dir=3");
    assert(dry && dry->power && dry->mode==2 && !dry->target_c);
    assert(room_mode(*dry)==enevina::PumpRoomMode::Other);
    const auto sensor=decode_sensors("ret=OK,htemp=21.2,otemp=-3.0,cmpfreq=45");
    assert(sensor && sensor->indoor_c==21.2 && sensor->outdoor_c==-3 && sensor->compressor_hz==45);
    const auto unknown=decode_sensors("ret=OK,htemp=-,otemp=--,cmpfreq=-");
    assert(unknown && !unknown->indoor_c && !unknown->outdoor_c && !unknown->compressor_hz);

    const enevina::PumpHeatingLimits limits{16,27,.5,true,true};
    const auto stop=set_request(*dry,{false,{},false},limits);
    assert(stop && *stop=="/aircon/set_control_info?pow=0&mode=2&stemp=M&shum=50&f_rate=5&f_dir=3");
    const auto heat=set_request(*dry,{true,21.5,true},limits);
    assert(heat && *heat=="/aircon/set_control_info?pow=1&mode=4&stemp=21.5&shum=0&f_rate=A&f_dir=1");
    assert(!set_request(*dry,{true,21.2,true},limits));
    assert(!set_request(*dry,{true,28.0,true},limits));
    assert(!set_request(*dry,{false,21.0,true},limits));
    assert(!matches(*dry,{true,21.5,true}));

    const auto heated=decode_state("ret=OK,pow=1,mode=4,stemp=21.5,shum=0");
    assert(heated && matches(*heated,{true,21.5,true}));
    const auto newer=set_request(*heated,{{},22,false},limits);
    assert(newer && *newer=="/aircon/set_control_info?pow=1&mode=4&stemp=22&shum=0");
    const auto stale=decode_state("ret=OK,pow=1,mode=4,stemp=21.0,shum=0");
    assert(stale && !matches(*stale,{true,21.5,true}));
    const auto stopped=decode_state("ret=OK,pow=0,mode=4,stemp=22,shum=0");
    const auto cooled=decode_state("ret=OK,pow=1,mode=3,stemp=22,shum=0");
    const auto adjusted=decode_state("ret=OK,pow=1,mode=4,stemp=22,shum=0");
    assert(stopped && cooled && adjusted);
    assert(!readback_matches(*heated,*stale,{{},22,false}));
    assert(!readback_matches(*heated,*stopped,{{},22,false}));
    assert(!readback_matches(*heated,*cooled,{{},22,false}));
    assert(readback_matches(*heated,*adjusted,{{},22,false}));
    assert(!same_request(*heated,*adjusted) && same_request(*heated,*heated));
    assert(changes_mode(*dry,{true,21.5,true}));
    assert(changes_mode(*heated,{false,{},false}));
    assert(!changes_mode(*heated,{{},22,false}));
    assert(!changes_mode(*stopped,{false,{},false}));
    assert(heat_target("21.5")==21.5 && !heat_target("21.1") && !heat_target("22&pow=1"));
    const auto model=model_limits("16.5","24.5","0.5");
    assert(model && model->accepts(18.5) && !model->accepts(16));
    assert(!model_limits("16.5","24","1") && !model_limits("24","16","0.5"));
    assert(!model_limits("16","24","0.25"));
    assert(accepted("ret=OK\r\n") && !accepted("ret=PARAM NG"));
    assert(!decode_state("ret=OK,pow=1,pow=0,mode=4,stemp=21.0"));
    assert(!decode_state("ret=OK,pow=2,mode=4,stemp=22"));
    assert(!decode_state("ret=OK,pow=1,mode=9,stemp=22"));
    assert(!decode_state(R"({"ret":"OK","pow":1})"));
    assert(!decode_state(std::string(max_body+1,'x')));
    const auto unsafe=decode_state("ret=OK,pow=1,mode=4,stemp=21.5,shum=0&pow=0");
    assert(unsafe && !set_request(*unsafe,{false,{},false},limits));
    const auto no_defaults=decode_state("ret=OK,pow=1,mode=2,stemp=M,shum=50,f_rate=5,f_dir=3");
    assert(no_defaults && !set_request(*no_defaults,{true,21.5,true},limits));
}
