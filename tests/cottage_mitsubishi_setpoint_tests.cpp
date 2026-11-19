#include "cottage/mitsubishi_climate_command.hpp"
#include <cassert>
#include <cstring>
#include <limits>
#include <initializer_list>
using namespace enevina::cottage;
int main() {
    const auto heat=mitsubishi_mode_request(MitsubishiMode::Heat);
    const auto off=mitsubishi_mode_request(MitsubishiMode::Off);
    assert(heat.valid && off.valid);
    assert(std::strcmp(heat.path.data(),
        "/climate/Mitsubishi%20Electric%20MSZ-FD25VA/set?mode=HEAT")==0);
    assert(std::strcmp(off.path.data(),
        "/climate/Mitsubishi%20Electric%20MSZ-FD25VA/set?mode=OFF")==0);
    for(double v : {16.0,16.5,18.0,18.5,20.0,23.5,26.0}) {
        const auto r=mitsubishi_setpoint_request(v);
        assert(r.valid && std::strlen(r.path.data())<r.path.size());
    }
    const auto half=mitsubishi_setpoint_request(18.5);
    assert(std::strstr(half.path.data(),"target_temperature=18.5")!=nullptr);
    for(double bad : {15.5,26.5,18.25,
                      std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::quiet_NaN()}) {
        const auto r=mitsubishi_setpoint_request(bad);
        assert(!r.valid && r.path[0]=='\0');
    }
    assert(mitsubishi_bridge_readback_matches(
        MitsubishiMode::Heat,18.5,true,"HEAT",18.5));
    assert(!mitsubishi_bridge_readback_matches(
        MitsubishiMode::Heat,18.5,false,"HEAT",18.5));
    assert(!mitsubishi_bridge_readback_matches(
        MitsubishiMode::Heat,18.5,true,"HEAT",18.0));
    assert(!mitsubishi_bridge_readback_matches(
        MitsubishiMode::Heat,18.5,true,"OFF",18.5));
    assert(mitsubishi_bridge_readback_matches(
        MitsubishiMode::Off,0,true,"OFF",21.0));
    assert(!mitsubishi_bridge_readback_matches(
        MitsubishiMode::Off,0,false,"OFF",21.0));
}
