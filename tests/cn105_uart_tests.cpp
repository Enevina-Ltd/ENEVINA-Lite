#include "pump/cn105_uart.hpp"
#include <cassert>
#include <initializer_list>

using namespace enevina;
using namespace enevina::cn105_uart;

static Frame sample(Query kind,std::initializer_list<uint8_t> payload) {
    auto f=query(kind);
    f.bytes[1]=command_data_reply;
    unsigned i=6;
    for(uint8_t b:payload)f.bytes[i++]=b;
    unsigned sum=0;
    for(i=0;i+1<f.size;++i)sum+=f.bytes[i];
    f.bytes[f.size-1]=static_cast<uint8_t>(0xfc-sum);
    return f;
}

int main() {
    const auto handshake=connect();
    assert(handshake.size==8 && handshake.bytes[0]==0xfc && handshake.bytes[1]==0x5a);
    assert(handshake.bytes[5]==0xca && handshake.bytes[6]==1 && handshake.bytes[7]==0xa8);
    const auto q=query(Query::Settings);
    assert(q.size==22 && q.bytes[1]==0x42 && q.bytes[5]==0x02 && q.bytes[21]==0x7b);
    auto set=sample(Query::Settings,{0,0,1,1,10,0,0,0,0,0,171});
    const auto heat=settings(set);
    assert(heat && heat->power==true && heat->mode==Mode::Heat && heat->target_c==21.5);
    assert(heat->encoding==TemperatureEncoding::HalfDegree && heating_mode(*heat)==PumpRoomMode::Heat);
    const auto earlier=settings(sample(Query::Settings,{0,0,0,1,9}));
    assert(earlier && earlier->power==false && earlier->target_c==22.);
    assert(earlier->encoding==TemperatureEncoding::WholeDegree && heating_mode(*earlier)==PumpRoomMode::Off);

    const auto t=room(sample(Query::Room,{0,0,11,0,122,170}));
    assert(t && t->indoor_c==21 && t->outdoor_c==-3);
    const auto c=operation(sample(Query::Operating,{0,0,26,1}));
    assert(c && c->running==true && c->compressor_hz==26);

    auto unknown=settings(sample(Query::Settings,{0,0,4,0x60,45,0,0,0,0,0,0x80}));
    assert(unknown && !unknown->power && !unknown->target_c && unknown->mode==Mode::Unknown);
    assert(!settings(sample(Query::Room,{})));
    set.bytes[10]^=1;
    assert(!settings(set));

    FrameReader parser;
    assert(!parser.append(0x42));
    assert(!parser.append(0xfc));
    assert(!parser.append(0xfc));
    auto actual=sample(Query::Room,{0,0,11});
    std::optional<Frame> decoded;
    for(size_t i=1;i<actual.size;++i)
        if(const auto out=parser.append(actual.bytes[i]))decoded=out;
    assert(decoded && room(*decoded)->indoor_c==21.);

    const PumpHeatingLimits limits{16,26,.5,true,true};
    const auto cmd=heat_command(*heat,{true,22.5,true},limits);
    assert(cmd && cmd->bytes[1]==0x41 && cmd->bytes[6]==7);
    assert(cmd->bytes[8]==1 && cmd->bytes[9]==1 && cmd->bytes[19]==173);
    const auto off=heat_command(*heat,{false,{},false},limits);
    assert(off && off->bytes[6]==1 && off->bytes[8]==0 && off->bytes[9]==0);
    assert(!heat_command(*earlier,{{},22.5,false},limits));
    assert(!readback_matches(*heat,*heat,{{},22.5,false}));
    auto adjusted=*heat;adjusted.target_c=22.5;
    assert(readback_matches(*heat,adjusted,{{},22.5,false}));
    adjusted.power=false;
    assert(!readback_matches(*heat,adjusted,{{},22.5,false}));
}
