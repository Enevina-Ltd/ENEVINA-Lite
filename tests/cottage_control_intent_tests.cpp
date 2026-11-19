#include "cottage/control_intent.hpp"
#include <cassert>
#include <cstring>
using namespace enevina::cottage;
int main() {
    SiteSettings settings{};
    std::strcpy(settings.cn105_url,"http://192.168.20.143/events");
    std::strcpy(settings.pump_adapter,"mitsubishi_cn105_esphome");
    uint8_t image_a[32]{};
    uint8_t image_b[32]{};
    image_a[0]=1;image_b[0]=2;
    auto on=control_intent_for(settings,true,image_a);
    assert(control_intent_restorable(on,settings,image_a));
    assert(!control_intent_restorable(on,settings,image_b));
    assert(!control_intent_restorable(on,settings,nullptr));
    auto off=control_intent_for(settings,false,image_a);
    assert(!control_intent_restorable(off,settings,image_a));
    auto wrong=settings;
    std::strcpy(wrong.cn105_url,"http://192.168.20.117/events");
    assert(!control_intent_restorable(on,wrong,image_a));
    wrong=settings;
    std::strcpy(wrong.pump_adapter,"other");
    assert(!control_intent_restorable(on,wrong,image_a));
    auto corrupt=on;
    corrupt.magic=0;
    assert(!control_intent_restorable(corrupt,settings,image_a));
    corrupt=on;
    corrupt.version=3;
    assert(!control_intent_restorable(corrupt,settings,image_a));
    corrupt=on;
    corrupt.size=0;
    assert(!control_intent_restorable(corrupt,settings,image_a));
    corrupt=on;
    corrupt.enabled=2;
    assert(!control_intent_restorable(corrupt,settings,image_a));
    corrupt=on;
    std::memset(corrupt.endpoint,'x',sizeof(corrupt.endpoint));
    assert(!control_intent_restorable(corrupt,settings,image_a));
    corrupt=on;
    std::memset(corrupt.adapter,'y',sizeof(corrupt.adapter));
    assert(!control_intent_restorable(corrupt,settings,image_a));
    corrupt=on;
    corrupt.image_sha256[31]^=0x80;
    assert(!control_intent_restorable(corrupt,settings,image_a));
}
