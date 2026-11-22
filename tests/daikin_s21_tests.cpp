#include "pump/daikin_s21.hpp"
#include <cassert>
#include <initializer_list>
using namespace enevina;
using namespace enevina::s21;
static Frame wire(std::initializer_list<uint8_t> bytes) {
    Frame f;
    for(const auto b:bytes)f.bytes[f.size++]=b;
    return f;
}
int main() {
    const auto q=query(read_climate);
    assert(q.size==5 && q.bytes[0]==2 && q.bytes[3]==0x77 && q.bytes[4]==3);
    const auto frame=wire({2,'G','1','1','4','G','A',0x65,3});
    const auto state=climate(frame);
    assert(state && state->power && state->mode==Mode::Heat && state->target_c==21.5);
    assert(heating_mode(*state)==PumpRoomMode::Heat);
    auto bad=frame;bad.bytes[4]='3';
    assert(!climate(bad));
    bad=frame;bad.bytes[0]=0;
    assert(!climate(bad));
    const auto t=wire({2,'S','H','5','1','2','+',0x5e,3});
    const auto o=wire({2,'S','a','5','2','0','-',0x78,3});
    assert(temperature(t,room_reply)==21.5);
    assert(temperature(o,outdoor_reply)==-2.5);
    assert(!temperature(t,outdoor_reply));

    Reader framer;
    assert(!framer.append(ack));
    assert(!framer.append(2));
    assert(!framer.append('A'));
    std::optional<Frame> out;
    for(size_t i=0;i<frame.size;++i)
        if(const auto value=framer.append(frame.bytes[i]))out=value;
    assert(out && climate(*out));
    // A sum of 0x03 is represented as 0x05 and does not truncate the frame.
    assert(response(wire({2,'F','4',0x89,5,3}),{'F','4'},1));

    const PumpHeatingLimits limits{18,30,.5,true,true};
    auto off=command(*state,{false,{},false},limits);
    assert(off && off->bytes[3]=='0' && off->bytes[4]=='4' && off->bytes[5]=='G' && off->bytes[6]=='A');
    auto heat=command(*state,{true,23,true},limits);
    assert(heat && heat->bytes[3]=='1' && heat->bytes[4]=='4' && heat->bytes[5]=='J' && heat->bytes[6]=='A');
    assert(!command(*state,{{},21.2,false},limits));
    assert(!command(*state,{{},31,false},limits));
    assert(!command(*state,{false,20,true},limits));
    assert(!readback_matches(*state,*state,{{},22.,false}));
    auto after=*state;
    after.target_c=22.;
    assert(readback_matches(*state,after,{{},22.,false}));
    after.mode=Mode::Cooling;
    assert(!readback_matches(*state,after,{{},22.,false}));
}
