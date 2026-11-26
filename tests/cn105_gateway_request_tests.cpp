#include "cottage/cn105_gateway_request.hpp"
#include "cottage/cn105_defrost.hpp"
#include <cstdio>
#include <cstdlib>
using namespace enevina;
using namespace enevina::cottage;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::abort();}}while(false)
static void test_defrost_guard() {
    using Signal=CN105DefrostSignal;
    using Source=CN105DefrostSource;
    using State=CN105DefrostState;
    CHECK(cn105_submode_report("DEFROST","DEFROST")==Signal::Defrost);
    CHECK(cn105_submode_report("DEFROST","NORMAL")==Signal::Defrost);
    CHECK(cn105_submode_report("NORMAL","")==Signal::Unknown);
    CHECK(cn105_submode_report(nullptr,"NORMAL")==Signal::Other);
    CHECK(cn105_submode_report("","")==Signal::Unknown);
    CHECK(cn105_submode_report("IDLE","IDLE")==Signal::Unknown);
    CHECK(cn105_action_signal("defrosting")==Signal::Defrost);
    CHECK(cn105_action_signal("HEATING")==Signal::Other);
    CHECK(cn105_action_signal("0")==Signal::Unknown);
    CN105DefrostHistory h;
    constexpr uint64_t t=10000;
    CHECK(h.view(t,1).state==State::Unknown);
    h.note(Source::ClimateAction,Signal::Other,t,1);
    CHECK(h.view(t,1).state==State::Unknown); // ordinary action isn't support
    h.note(Source::SubMode,Signal::Other,t,1);
    CHECK(h.view(t,1).state==State::ReportedOther);
    h.note(Source::SubMode,Signal::Unknown,t+1,1);
    CHECK(h.view(t+1,1).state==State::Unknown);
    h.note(Source::SubMode,Signal::Defrost,t+100,1);
    const auto active=h.view(t+100,1);
    CHECK(active.state==State::ReportedDefrost && active.ever_reported_defrost);
    CHECK(cn105_defrost_defer_write(active,true));
    CHECK(!cn105_defrost_defer_write(active,false));
    h.note(Source::ClimateAction,Signal::Other,t+101,1);
    h.note(Source::SubMode,Signal::Other,t+99,1); // old in-flight GET
    h.note(Source::SubMode,Signal::Unknown,t+102,1);
    CHECK(h.view(t+102,1).defer_write);
    CHECK(h.view(t+100+180001,1).state==State::SignalLost);
    CHECK(h.view(t+100+720001,1).state==State::Overdue);
    CHECK(!h.view(t+100+720001,1).defer_write);
    h.note(Source::SubMode,Signal::Defrost,t+725000,1);
    CHECK(h.view(t+725000,1).state==State::Overdue); // cached repeat cannot prolong
    CHECK(h.view(t+725000,1).generation==1);
    h.note(Source::SubMode,Signal::Other,t+726000,1);
    CHECK(h.view(t+726000,1).submode_ends==0); // overdue not validated completion
    h.note(Source::SubMode,Signal::Defrost,t+730000,1);
    CHECK(h.view(t+730000,1).reports==3);
    CHECK(h.view(t+730000,1).generation==2);
    CHECK(!h.view(t+730000,2).defer_write); // unrelated pump
    h.note(Source::ClimateAction,Signal::Defrost,t+731000,1);
    h.note(Source::SubMode,Signal::Other,t+732000,1);
    CHECK(h.view(t+732000,1).state==State::ReportedDefrost);
    h.note(Source::ClimateAction,Signal::Other,t+733000,1);
    CHECK(h.view(t+733000,1).state==State::Recovery);
    h.note(Source::ClimateAction,Signal::Other,t+734000,1);
    CHECK(!h.view(t+793000,1).defer_write); // repeat doesn't extend recovery
    const auto completed=h.view(t+793000,1);
    CHECK(completed.reports==4);
    CHECK(completed.submode_starts==2 && completed.submode_ends==1);
    CHECK(completed.action_starts==1 && completed.action_ends==1);
    CHECK(completed.submode_last_duration_s==2 && completed.action_last_duration_s==2);
    h.note(Source::SubMode,Signal::Unknown,t+794000,2);
    CHECK(!h.view(t+794000,2).ever_reported_defrost);
}
int main(){
    test_defrost_guard();
    {
        CHECK(cn105_gateway_mode("OFF")==PumpRoomMode::Off);
        CHECK(cn105_gateway_mode("HEAT")==PumpRoomMode::Heat);
        for(const char* m:{"COOL","DRY","AUTO","HEAT_COOL","FAN_ONLY"})
            CHECK(cn105_gateway_mode(m)==PumpRoomMode::Other);
        CHECK(cn105_gateway_mode("bad")==PumpRoomMode::Unknown);
        CHECK(cn105_gateway_mode(nullptr)==PumpRoomMode::Unknown);
        CN105GatewayRequestHistory history;
        constexpr int64_t utc=1800000000;
        CHECK(history.note_mode(PumpRoomMode::Other,0,utc,10000,1,1));
        const auto other=history.snapshot(utc,10000,1,1);
        CHECK(other.mode==PumpRoomMode::Other);
        CHECK(history.note_mode(PumpRoomMode::Off,0,utc+60,70000,1,1));
        const auto off=history.snapshot(utc+60,70000,1,1);
        CHECK(off.mode==PumpRoomMode::Off);
        CHECK(!same_cn105_gateway_request(other,off));
        CHECK(!history.note_mode(PumpRoomMode::Unknown,0,utc+61,71000,1,1));
        CHECK(history.snapshot(utc+61,71000,1,1).source==PumpStateSource::None);
    }

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
    // An automatic mode command is usable only when exactly one confirmed
    // dispatch explains the same independently observed single switch.
    for(bool old_heat : {false,true}) {
        CN105GatewayRequestHistory tracker;
        CHECK(tracker.note(old_heat,16,t,ms,1,1));
        auto begin=tracker.snapshot(t,ms,1,1);
        for(unsigned i=1;i<=15;++i) CHECK(tracker.note(i<6?old_heat:!old_heat,16,t+60*i,ms+60000*i,1,1));
        const auto end=tracker.snapshot(t+900,ms+900000,1,1);
        const CN105SlotCommands command{1,1,0,old_heat?ControlStep::SetOff:ControlStep::SetHeat,t+357,t+360,16};
        const auto good=cn105_learning_request(tracker,begin,end,command);
        CHECK(good.valid && std::fabs(good.before_fraction-358.5/900.0)<1e-9);
        auto external=cn105_learning_request(tracker,begin,end,{});
        CHECK(external.valid && std::fabs(external.before_fraction-330.0/900.0)<1e-9);
        for(int bad=0;bad<9;++bad) {
            auto cmd=command;
            if(bad==0)cmd.sent=2;
            if(bad==1)cmd.confirmed=0;
            if(bad==2)cmd.failed=1;
            if(bad==3)cmd.step=ControlStep::Setpoint;
            if(bad==4)cmd.sent_utc=t-1;
            if(bad==5)cmd.sent_utc=t+361;
            if(bad==6)cmd.ack_utc=t+359;
            if(bad==7)cmd.ack_utc=t+901;
            if(bad==8)cmd.step=old_heat?ControlStep::SetHeat:ControlStep::SetOff;
            CHECK(!cn105_learning_request(tracker,begin,end,cmd).valid);
        }
        if(!old_heat) {
            auto cmd=command;cmd.target_c=17;
            CHECK(!cn105_learning_request(tracker,begin,end,cmd).valid);
        }
        tracker.invalidate();
        CHECK(!cn105_learning_request(tracker,begin,end,command).valid);
    }
    {
        CN105GatewayRequestHistory tracker;
        CHECK(tracker.note(false,16,t,ms,1,1));
        const auto begin=tracker.snapshot(t,ms,1,1);
        for(unsigned i=1;i<=15;++i)CHECK(tracker.note(false,16,t+60*i,ms+60000*i,1,1));
        const auto end=tracker.snapshot(t+900,ms+900000,1,1);
        auto quiet=cn105_learning_request(tracker,begin,end,{});
        CHECK(quiet.valid && quiet.before_fraction==-1);
        auto uncertain_start=begin;uncertain_start.command_unconfirmed=true;
        auto uncertain_end=end;uncertain_end.command_unconfirmed=true;
        CHECK(!cn105_learning_request(tracker,uncertain_start,end,{}).valid);
        CHECK(!cn105_learning_request(tracker,begin,uncertain_end,{}).valid);
        CN105SlotCommands failure{};failure.failed=1;
        CHECK(!cn105_learning_request(tracker,begin,end,failure).valid);
        CN105SlotCommands setpoint{1,1,0,ControlStep::Setpoint,t+400,t+401,16};
        CHECK(!cn105_learning_request(tracker,begin,end,setpoint).valid);
        auto too_short=end;too_short.evidence.received_utc=t+839;
        CHECK(!cn105_learning_request(tracker,begin,too_short,{}).valid);
        auto forged=end;forged.setpoint_c=17;
        CHECK(!cn105_learning_request(tracker,begin,forged,{}).valid);
        tracker.invalidate();
        CHECK(!cn105_learning_request(tracker,begin,end,{}).valid);
    }
    for(bool distinct_ms:{true,false}) {
        CN105GatewayRequestHistory tracker;
        CHECK(tracker.note(false,16,t,ms,1,1));
        const auto begin=tracker.snapshot(t,ms,1,1);
        for(unsigned i=1;i<=6;++i)CHECK(tracker.note(false,16,t+60*i,ms+60000*i,1,1));
        CHECK(tracker.note(true,16,t+360,ms+360000+(distinct_ms?600:0),1,1));
        for(unsigned i=7;i<=15;++i)CHECK(tracker.note(true,16,t+60*i,ms+60000*i,1,1));
        const auto end=tracker.snapshot(t+900,ms+900000,1,1);
        const CN105SlotCommands cmd{1,1,0,ControlStep::SetHeat,t+360,t+360,16};
        const auto result=cn105_learning_request(tracker,begin,end,cmd);
        CHECK(result.valid==distinct_ms);
        if(distinct_ms)CHECK(std::fabs(result.before_fraction-0.4)<1e-9);
    }
    std::puts("CN105 standard gateway request regressions PASS");
}
