#include "pump/gree_lan_protocol.hpp"
#include <cassert>
#include <string>

using namespace enevina::gree;

int main() {
    const auto unit=device_id("01:23:45:AB:CD:EF");
    assert(unit && *unit=="012345abcdef");
    assert(!device_id("012345abcdex"));
    assert(!device_id("000000000000"));
    assert(!device_id("ffffffffffff"));

    const std::string v1=
        R"({"t":"pack","i":1,"uid":0,"cid":"012345abcdef","tcid":"","pack":"S8GlvPD6Cdv807vL1HH1f9Ocnn+z1Spc3U8K7KA8VelDsKxqUny00WOzqiYzxnfY"})";
    const std::string v2=
        R"({"t":"pack","i":1,"uid":0,"cid":"012345abcdef","tcid":"","pack":"JtoKliwtrZWlpNCVNygWFZVjuNIQ0RPzYQY3Wb+NhLU=","tag":"0AXyAlTkVjzIAehZZvB5sg=="})";
    assert(discovery(v1)->cipher==Cipher::Ecb);
    assert(discovery(v2)->cipher==Cipher::Gcm);
    assert(discovery(v2)->id==*unit);
    auto tampered=v2;
    tampered[tampered.find("0AX")] = 'A';
    assert(!discovery(tampered));
    assert(!discovery(v1+"{}"));
    assert(!discovery(v1.substr(0,v1.size()-1)+R"(,"i":0})"));

    for(const auto variant:{Cipher::Ecb,Cipher::Gcm}) {
        const auto binding_reply=pack(variant,binding_key(variant),true,*unit,"app",
            R"({"t":"bindok","mac":"012345abcdef","key":"test-key-16-byte","r":200})");
        assert(!binding_reply.empty());
        const auto key=bound_key(variant,*unit,binding_reply);
        assert(key);
        assert(!bound_key(variant,"012345abcdee",binding_reply));
        const auto refreshed_binding=pack(variant,binding_key(variant),true,*unit,"app",
            R"({"t":"bindok","mac":"012345abcdef","key":"new-key-16-bytes","r":200})");
        const auto new_key=bound_key(variant,*unit,refreshed_binding);
        assert(new_key && *new_key!=*key);
        const auto newer=pack(variant,*new_key,false,*unit,"app",
            R"({"t":"dat","cols":["Pow","Mod"],"dat":[0,4]})");
        assert(!parse_status(variant,*key,*unit,newer));
        assert(parse_status(variant,*new_key,*unit,newer));
        const auto bind=bind_request(variant,*unit);
        const auto decoded_bind=unpack(variant,binding_key(variant),true,"app",*unit,bind);
        assert(decoded_bind && decoded_bind->find("\"t\":\"bind\"")!=std::string::npos);
        const auto query=status_request(variant,*key,*unit);
        const auto decoded_query=unpack(variant,*key,false,"app",*unit,query);
        assert(decoded_query && decoded_query->find("\"TemSen\"")!=std::string::npos);

        const auto read=[&](const char* body,const char* from="012345abcdef") {
            return parse_status(variant,*key,*unit,pack(variant,*key,false,from,"app",body));
        };
        const auto reading=read(R"({"t":"dat","mac":"012345abcdef","cols":["Pow","Mod","TemUn","SetTem","TemSen","StHt"],"dat":[1,4,0,21,62,0]})");
        assert(reading && reading->power && reading->mode==Mode::Heat);
        assert(reading->setpoint_c==21 && reading->indoor_c==22 && reading->frost==false);
        assert(!read(R"({"t":"dat","mac":"012345abcdef","cols":["Pow","Mod"],"dat":[1,4]})","012345abcdee"));
        assert(!read(R"({"t":"dat","mac":"012345abcdee","cols":["Pow","Mod"],"dat":[1,4]})"));
        assert(!read(R"({"t":"res","opt":["Pow","Mod"],"p":[1,4]})"));
        assert(!read(R"({"t":"dat","cols":["Pow","Mod"],"dat":[1]})"));
        assert(!read(R"({"t":"dat","cols":["Pow","Pow","Mod"],"dat":[1,0,4]})"));
        assert(!read(R"({"t":"dat","cols":["Pow","Mod"],"dat":[true,4]})"));
        const auto unknown=read(R"({"t":"dat","cols":["Pow","Mod","SetTem","TemSen"],"dat":[0,4,21,0]})");
        assert(unknown && !unknown->power && !unknown->setpoint_c && !unknown->indoor_c && !unknown->frost);
        const auto fahrenheit=read(R"({"t":"dat","cols":["Pow","Mod","SetTem","TemUn"],"dat":[1,4,21,1]})");
        assert(fahrenheit && !fahrenheit->setpoint_c);

        // Commands carry only modified fields, and replies to commands are
        // never accepted as fresh status or as verification.
        const Patch setpoint{std::nullopt,std::nullopt,22};
        const auto command=command_request(variant,*key,*unit,setpoint);
        const auto decoded=unpack(variant,*key,false,"app",*unit,command);
        assert(decoded && decoded->find("\"opt\":[\"SetTem\"]")!=std::string::npos);
        assert(!confirmed(setpoint,*reading));
        const enevina::PumpHeatingLimits pilot{18,26,1,true,true};
        assert(permits_heating_patch(setpoint,*reading,pilot));
        assert(!permits_heating_patch({std::nullopt,std::nullopt,27},*reading,pilot));
        assert(!permits_heating_patch({true,Mode::Cool,22},*reading,pilot));
        assert(heating_room_mode(*reading)==enevina::PumpRoomMode::Heat);
        auto frost=*reading;frost.frost=true;
        assert(!permits_heating_patch({false,{},{}},frost,pilot));
        assert(heating_room_mode(frost)==enevina::PumpRoomMode::Unknown);
        frost.frost.reset();
        assert(!permits_heating_patch(setpoint,frost,pilot));
        assert(heating_room_mode(frost)==enevina::PumpRoomMode::Unknown);
        auto outside=*reading;outside.setpoint_c=27;
        assert(permits_heating_patch({false,{},{}},outside,pilot));
        constexpr uint32_t dwell=30*60*1000;
        assert(!dwell_allows({false,{},{}},*reading,1000,dwell));
        assert(dwell_allows({false,{},{}},*reading,dwell,dwell));
        assert(dwell_allows(setpoint,*reading,1000,dwell));
        auto off=*reading;off.power=false;
        assert(!dwell_allows({true,Mode::Heat,22},off,dwell-1,dwell));
        auto cooling=*reading;cooling.mode=Mode::Cool;
        assert(!dwell_allows({true,Mode::Heat,22},cooling,0,dwell));
        assert(!permits_heating_patch(setpoint,cooling,pilot));
        assert(permits_heating_patch({true,Mode::Heat,22},cooling,pilot));
        assert(permits_heating_patch({false,{},{}},cooling,pilot));
        assert(permits_heating_patch(setpoint,off,pilot));
        const auto changed=read(R"({"t":"dat","cols":["Pow","Mod","TemUn","SetTem"],"dat":[1,4,0,22]})");
        assert(changed && confirmed(setpoint,*changed));
        auto matching=*reading;matching.setpoint_c=22;
        assert(readback_matches(setpoint,*reading,matching));
        matching.mode=Mode::Cool;assert(!readback_matches(setpoint,*reading,matching));
        matching=*reading;matching.setpoint_c=22;matching.power=false;
        assert(!readback_matches(setpoint,*reading,matching));
        assert(readback_matches({false,{},{}},*reading,matching));
        matching=*reading;matching.setpoint_c=22;matching.frost.reset();
        assert(!readback_matches(setpoint,*reading,matching));
        assert(command_request(variant,*key,*unit,{false,Mode::Heat,30}).empty());
        assert(command_request(variant,*key,*unit,{std::nullopt,std::nullopt,31}).empty());
    }
}
