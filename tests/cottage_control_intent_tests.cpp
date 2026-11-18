#include "cottage/control_intent.hpp"
#include <cassert>
#include <cstring>
using namespace enevina::cottage;
int main() {
    SiteSettings settings{};
    std::strcpy(settings.cn105_url,"http://192.168.20.143/events");
    std::strcpy(settings.pump_adapter,"mitsubishi_cn105_esphome");
    auto on=control_intent_for(settings,true);
    assert(control_intent_restorable(on,settings));
    auto off=control_intent_for(settings,false);
    assert(!control_intent_restorable(off,settings));
    auto wrong=settings;
    std::strcpy(wrong.cn105_url,"http://192.168.20.117/events");
    assert(!control_intent_restorable(on,wrong));
    wrong=settings;
    std::strcpy(wrong.pump_adapter,"other");
    assert(!control_intent_restorable(on,wrong));
    auto corrupt=on;
    corrupt.magic=0;
    assert(!control_intent_restorable(corrupt,settings));
    corrupt=on;
    corrupt.version=2;
    assert(!control_intent_restorable(corrupt,settings));
    corrupt=on;
    corrupt.size=0;
    assert(!control_intent_restorable(corrupt,settings));
    corrupt=on;
    corrupt.enabled=2;
    assert(!control_intent_restorable(corrupt,settings));
    corrupt=on;
    std::memset(corrupt.endpoint,'x',sizeof(corrupt.endpoint));
    assert(!control_intent_restorable(corrupt,settings));
    corrupt=on;
    std::memset(corrupt.adapter,'y',sizeof(corrupt.adapter));
    assert(!control_intent_restorable(corrupt,settings));
}
