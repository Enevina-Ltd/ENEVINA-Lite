#include "integrations/cn105-esphome/enevina_verified_poll.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using enevina_cn105::VerifiedPoll;

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::abort(); } } while (false)

namespace {
constexpr int64_t utc=1800000000;
using Frame=std::array<uint8_t,22>;

Frame frame(uint8_t code,uint8_t third,uint8_t fourth=0,uint8_t fifth=0) {
    Frame b{};
    b[0]=0xfc; b[1]=0x62; b[2]=0x01; b[3]=0x30; b[4]=0x10;
    b[5]=code; b[8]=third; b[9]=fourth; b[10]=fifth;
    return b;
}
Frame checksum(Frame b) {
    uint8_t sum=0;
    for (size_t i=0;i<b.size()-1;++i)sum+=b[i];
    b.back()=static_cast<uint8_t>(0xfc-sum);
    return b;
}
bool read(VerifiedPoll& p,Frame bytes,uint64_t ms,int64_t t) {
    return p.receive(bytes.data(),bytes.size(),ms,t);
}
bool cycle(VerifiedPoll& p,int64_t t,uint64_t start,uint8_t power=1) {
    p.begin();
    p.sent(0x02,start);
    CHECK(!read(p,checksum(frame(2,power,1,15)),start+100,t));
    p.sent(0x03,start+100);
    CHECK(!read(p,checksum(frame(3,5)),start+200,t));
    p.sent(0x06,start+200);
    return read(p,checksum(frame(6,power?35:0,power)),start+300,t+1);
}
}

int main() {
    VerifiedPoll p;
    CHECK(!cycle(p,utc,1000));
    p.set_epoch(7);
    CHECK(cycle(p,utc,2000));
    CHECK(p.snapshot().sequence==1);
    CHECK(p.snapshot().oldest_utc==utc && p.snapshot().latest_utc==utc+1);
    CHECK(p.snapshot().indoor_half_c==30 && p.snapshot().target_half_c==32);
    CHECK(p.snapshot().compressor_hz==35);
    CHECK(std::strcmp(p.snapshot().mode,"HEAT")==0);
    CHECK(cycle(p,utc+2,3000,0));
    CHECK(p.snapshot().sequence==2 && p.snapshot().compressor_hz==0);
    CHECK(std::strcmp(p.snapshot().mode,"OFF")==0);

    auto settings=checksum(frame(2,1,1,15));
    auto room=checksum(frame(3,5));
    auto status=checksum(frame(6,35,1));
    p.begin(); p.sent(2,4000);
    settings.back()^=1;
    CHECK(!read(p,settings,4100,utc+4));
    p.sent(3,4100);
    CHECK(!read(p,room,4200,utc+4));
    p.sent(6,4200);
    CHECK(!read(p,status,4300,utc+5));
    CHECK(p.snapshot().sequence==2);

    // A late unmatched reply must not replace a missing matching response.
    settings.back()^=1;
    p.begin();p.sent(2,5000);
    CHECK(!read(p,settings,5100,utc+5));
    CHECK(!read(p,room,5150,utc+5));
    p.sent(3,5200);
    CHECK(!read(p,room,5250,utc+5));
    p.discard();
    p.sent(6,5250);
    CHECK(!read(p,status,5300,utc+6));
    CHECK(p.snapshot().sequence==2);

    // Reject late, truncated, and backward-clock packets even with plausible data.
    p.begin();p.sent(2,6000);
    CHECK(!read(p,settings,66001,utc+6));
    p.sent(3,66001);
    CHECK(!p.receive(room.data(),room.size()-1,66101,utc+6));
    p.sent(6,66101);
    CHECK(!read(p,status,66201,utc+7));
    CHECK(!cycle(p,utc-10,67000));
    CHECK(p.snapshot().sequence==2);

    p.begin();p.sent(2,68000);
    CHECK(!read(p,settings,68100,utc+9));
    p.sent(3,68100);
    CHECK(!read(p,room,68200,utc+9));
    CHECK(!read(p,room,68210,utc+9)); // duplicate
    p.sent(6,68215);
    CHECK(!read(p,status,68220,utc+10));
    CHECK(cycle(p,utc+11,69000));
    CHECK(p.snapshot().sequence==3);
    std::puts("PASS bridge-origin CN105 completed UART polls");
}
