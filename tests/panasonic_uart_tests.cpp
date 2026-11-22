#include "pump/panasonic_uart.hpp"
#include <cassert>
#include <initializer_list>
using namespace enevina;
using namespace enevina::panasonic;

static Packet verify(Packet p) {
    uint8_t sum=0;
    for(size_t i=0;i+1<p.size;++i)sum+=p.bytes[i];
    p.bytes[p.size-1]=static_cast<uint8_t>(0-sum);
    Reader reader;
    for(size_t i=0;i+1<p.size;++i)assert(!reader.append(p.bytes[i]));
    const auto out=reader.append(p.bytes[p.size-1]);
    assert(out && out->size==p.size);
    return *out;
}
int main() {
    const PumpHeatingLimits limits{16,30,.5,true,true};
    const auto poll=cnt_poll();
    assert(poll.size==13 && poll.bytes[0]==0x70 && poll.bytes[1]==10 && poll.bytes[12]==0x86);
    assert(!climate(poll,Family::Cnt));
    assert(next_sequence(0xfe)==1 && next_sequence(1)==2 && next_sequence(0)==1);

    Packet cnt;cnt.size=32;cnt.bytes[0]=0x70;cnt.bytes[1]=29;
    cnt.bytes[2]=0x44;cnt.bytes[3]=43;cnt.bytes[5]=0x70;cnt.bytes[6]=0xde;
    cnt.bytes[18]=0x80;cnt.bytes[21]=20;cnt.bytes[19]=251;
    const auto c=climate(verify(cnt),Family::Cnt);
    assert(c && c->power && c->mode==Mode::Heat && c->target_c==21.5 && c->indoor_c==20 && c->outdoor_c==-5);
    auto off=command(*c,Patch{false,{},{}},limits);
    assert(off && off->bytes[0]==0xf0 && off->bytes[2]==0x40 && off->bytes[5]==0x70 && off->bytes[6]==0xde);
    auto changed=*c;changed.power=false;
    assert(readback_matches(*c,changed,Patch{false,{},{}}));
    assert(!readback_matches(*c,changed,Patch{{},{},21.5}));
    changed.indoor_c=22;changed.power=true;
    assert(same_settings(*c,changed));
    cnt.bytes[14]=2;
    assert(!command(*climate(verify(cnt),Family::Cnt),Patch{false,{},{}},limits));

    Packet w;w.size=125;w.bytes[0]=0x5a;w.bytes[1]=0x15;
    w.bytes[2]=0x10;w.bytes[3]=0x89;w.bytes[5]=118;
    w.bytes[14]=0x30;w.bytes[18]=0x43;w.bytes[22]=42;w.bytes[62]=20;w.bytes[66]=0x80;
    const auto replay=verify(w);
    const auto state=climate(replay,Family::Wlan,0x15);
    assert(state && state->power && state->mode==Mode::Heat && state->target_c==21 && !state->outdoor_c);
    assert(!climate(replay,Family::Wlan,0x16));
    w.bytes[22]=0x80;
    assert(!climate(verify(w),Family::Wlan,0x15)->target_c);
    w.bytes[3]=0x88;
    assert(!climate(verify(w),Family::Wlan,0x15));
    const auto set=command(*state,Patch{true,true,22.5},limits,0x16);
    assert(set && set->is_wlan(0x10,0x08) && set->sequence()==0x16);
    assert(set->bytes[10]==3 && set->bytes[12]==0xb0 && set->bytes[14]==0x43 &&
           set->bytes[16]==0x80 && set->bytes[18]==0x30 && set->bytes[20]==0x31 && set->bytes[22]==45);
    assert(!command(*state,Patch{{},{},22.25},limits,0x16));
    assert(wlan_poll(0x17).is_wlan(0x10,0x09));
    for(unsigned i=1;i<=16;++i) {
        const auto request=wlan_handshake(i,2);
        if(i!=14 && i!=15)assert(request.packet.size>=7);
        assert(request.packet.sequence()==2);
    }
    Packet ping;ping.bytes[0]=0x5a;ping.bytes[1]=99;ping.bytes[2]=1;ping.bytes[3]=1;ping.size=7;
    const auto pong=wlan_reply(ping);
    assert(pong.is_wlan(1,0x81) && pong.sequence()==99);
    auto corrupt=replay;corrupt.bytes[70]^=4;
    Reader reader;
    for(size_t i=0;i<corrupt.size;++i)assert(!reader.append(corrupt.bytes[i]));
    for(size_t i=0;i+1<replay.size;++i)assert(!reader.append(replay.bytes[i]));
    assert(reader.append(replay.bytes[replay.size-1])->sequence()==0x15);
}
