#include "cottage/cn105_gateway_request.hpp"
#include <cstdio>
#include <cstdlib>
using namespace enevina;
using namespace enevina::cottage;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::abort();}}while(false)
int main(){
    CN105GatewayRequestHistory h;
    constexpr int64_t t=1800000000;
    constexpr uint64_t ms=100000;
    CHECK(h.snapshot(t,ms,1,1).source==PumpStateSource::None);
    CHECK(h.note(false,16,t,ms,1,1));
    auto a=h.snapshot(t,ms,1,1);
    CHECK(a.source==PumpStateSource::Gateway && a.mode==PumpRoomMode::Off);
    CHECK(!a.evidence.source_time_verified && a.evidence.source_utc==0);
    CHECK(a.request_minutes==0 && a.mode_minutes==0);
    for(unsigned i=1;i<=14;i++) CHECK(h.note(false,16,t+60*i,ms+60000*i,1,1));
    auto b=h.snapshot(t+840,ms+840000,1,1);
    CHECK(b.mode_minutes==14 && b.request_minutes==14 && same_cn105_gateway_request(a,b));
    CHECK(h.note(false,17,t+841,ms+841000,1,1));
    CHECK(same_cn105_gateway_request(a,h.snapshot(t+841,ms+841000,1,1)));
    CHECK(h.note(true,17,t+842,ms+842000,1,1));
    auto c=h.snapshot(t+842,ms+842000,1,1);
    CHECK(c.mode==PumpRoomMode::Heat && c.request_minutes==0 && !same_cn105_gateway_request(b,c));
    CHECK(h.note(true,17.5,t+843,ms+843000,1,1));
    auto d=h.snapshot(t+843,ms+843000,1,1);
    CHECK(!same_cn105_gateway_request(c,d) && d.request_minutes==0);
    CHECK(h.snapshot(t+1024,ms+1024000,1,1).source==PumpStateSource::None);
    CHECK(h.note(true,17.5,t+1024,ms+1024000,1,1));
    auto e=h.snapshot(t+1024,ms+1024000,1,1);
    CHECK(e.request_minutes==0 && !same_cn105_gateway_request(d,e));
    CHECK(h.snapshot(t+1024,ms+1024000,2,1).source==PumpStateSource::None);
    CHECK(h.snapshot(t+1024,ms+1024000,1,2).source==PumpStateSource::None);
    CHECK(h.note(true,17.5,t+1025,ms+1025000,1,2));
    CHECK(!same_cn105_gateway_request(e,h.snapshot(t+1025,ms+1025000,1,2)));
    CHECK(h.snapshot(t+1024,ms+1025000,1,2).source==PumpStateSource::None);
    h.invalidate();
    CHECK(h.snapshot(t+1026,ms+1026000,1,2).source==PumpStateSource::None);
    CHECK(!h.note(true,90,t+1027,ms+1027000,1,2));
    CHECK(h.snapshot(t+1027,ms+1027000,1,2).source==PumpStateSource::None);
    CHECK(h.note(false,16,t+1028,ms+1028000,1,2));
    auto f=h.snapshot(t+1028,ms+1028000,1,2);
    CHECK(h.note(true,16,t+1029,ms+1029000,1,2));
    CHECK(h.note(false,16,t+1030,ms+1030000,1,2));
    CHECK(!same_cn105_gateway_request(f,h.snapshot(t+1030,ms+1030000,1,2)));
    // Do not infer elapsed request continuity after the last observed GET.
    CHECK(h.snapshot(t+1149,ms+1149000,1,2).request_minutes==0);
    CHECK(h.note(false,16,t+1029,ms+1149000,1,2));
    auto rollback=h.snapshot(t+1029,ms+1149000,1,2);
    CHECK(rollback.request_minutes==0 && !same_cn105_gateway_request(f,rollback));
    // A single mid-slot change is observable; two changes or a broken
    // connection are not. The estimated midpoint has one-poll uncertainty.
    {
        CN105GatewayRequestHistory tracker;
        CHECK(tracker.note(false,16,t,ms,1,1));
        const auto begin=tracker.snapshot(t,ms,1,1);
        for(unsigned i=1;i<=15;++i) CHECK(tracker.note(i>=6,16,t+60*i,ms+60000*i,1,1));
        const auto end=tracker.snapshot(t+900,ms+900000,1,1);
        double f=0;
        CHECK(tracker.single_mode_transition(begin,end,f));
        CHECK(std::fabs(f-5.5/15.0)<1e-9);
        tracker.invalidate();
        CHECK(!tracker.single_mode_transition(begin,end,f));
    }
    for(int scenario=0;scenario<4;++scenario) {
        CN105GatewayRequestHistory tracker;
        CHECK(tracker.note(false,16,t,ms,1,1));
        const auto begin=tracker.snapshot(t,ms,1,1);
        for(unsigned i=1;i<=15;++i) {
            if(scenario==0 && i>=3 && i<=6)continue; // gap
            if(scenario==1 && i==6)tracker.invalidate(); // failed GET
            const bool heat=scenario==2?(i>=3 && i<6)||i>=9:i>=6;
            const unsigned endpoint=scenario==3 && i>=6?2:1;
            CHECK(tracker.note(heat,16,t+60*i,ms+60000*i,endpoint,1));
        }
        const auto end=tracker.snapshot(t+900,ms+900000,scenario==3?2:1,1);
        double f=0;
        CHECK(!tracker.single_mode_transition(begin,end,f));
    }
    std::puts("CN105 standard gateway request regressions PASS");
}
