// History ring, recorder, range queries and flash persistence (torn writes,
// corruption, compaction, wear bounds). Memory-backed files stand in for LittleFS.
#include "cottage/history_persist.hpp"
#include "cottage/ota_history_policy.hpp"
#include "cottage/history_query.hpp"
#include "forecast/met_norway.hpp"      // strict JSON validator for the output
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace enevina::cottage;
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)

struct MemFile : FileBackend {
    std::vector<uint8_t> data;
    bool present=false;
    bool fail_append=false,fail_replace=false,torn_append=false;
    size_t appends=0,replaces=0;
    size_t bytes_written=0;
    bool exists() override {return present;}
    size_t size() override {return data.size();}
    bool read(size_t off,void* out,size_t n) override {
        if(off+n>data.size()) return false;
        std::memcpy(out,data.data()+off,n);return true;
    }
    bool append(const void* d,size_t n) override {
        if(fail_append) return false;
        const uint8_t* p=static_cast<const uint8_t*>(d);
        if(torn_append) {data.insert(data.end(),p,p+n/2+5);present=true;bytes_written+=n/2+5;return false;}   // power cut mid-record
        data.insert(data.end(),p,p+n);present=true;++appends;bytes_written+=n;return true;
    }
    std::vector<uint8_t> pending;
    bool pending_open=false;
    bool replace_begin() override {if(fail_replace) return false;pending.clear();pending_open=true;return true;}
    bool replace_write(const void* d,size_t n) override {
        if(!pending_open) return false;
        const uint8_t* p=static_cast<const uint8_t*>(d);
        pending.insert(pending.end(),p,p+n);return true;
    }
    bool replace_commit() override {
        if(!pending_open||fail_replace) return false;
        data=pending;present=true;++replaces;bytes_written+=pending.size();pending_open=false;return true;
    }
    void replace_abort() override {pending.clear();pending_open=false;}
};

static void mount_failure_never_formats_nonblank_partition() {
    std::vector<uint8_t> raw(4097,0xFF);
    auto read=[&](size_t off,void* dst,size_t n) {
        if(off+n>raw.size())return false;
        std::memcpy(dst,raw.data()+off,n);return true;
    };
    CHECK(history_partition_is_blank(raw.size(),read));
    raw.back()=0x01; // Last byte: scanning only a flash prefix is unsafe.
    CHECK(!history_partition_is_blank(raw.size(),read));
    raw.back()=0xFF;raw[0]=0x00;
    CHECK(!history_partition_is_blank(raw.size(),read));
    raw[0]=0xFF;
    int reads=0;
    CHECK(!history_partition_is_blank(raw.size(),[&](size_t off,void* dst,size_t n) {
        ++reads;
        if(off>=512)return false;
        return read(off,dst,n);
    }));
    CHECK(reads>=2);
    CHECK(!history_partition_is_blank(0,read));
}
static HistoryRecord rec(uint32_t slot,double indoor=21.0) {
    HistoryRecord r;
    r.slot=slot;
    r.indoor_c100=static_cast<int16_t>(std::lround(indoor*100));
    r.price_e5=static_cast<int32_t>(std::lround(0.1*1e5));
    return r;
}
constexpr uint32_t kBase=1900000;   // an arbitrary slot number (~ year 2030, no meaning)

static void ring_semantics() {
    HistoryStore s(100);
    CHECK(s.empty()&&s.get(5)==nullptr);
    CHECK(s.put(rec(kBase))&&s.put(rec(kBase+1))&&!s.put(rec(kBase+1))&&!s.put(rec(kBase)));   // monotone only
    CHECK(!s.put(rec(0)));
    CHECK(s.get(kBase)&&s.get(kBase+1)&&!s.get(kBase+2));
    CHECK(s.put(rec(kBase+10)));                                      // a gap stays a gap
    CHECK(s.get(kBase+5)==nullptr);
    for(uint32_t i=11;i<=150;++i) s.put(rec(kBase+i));
    CHECK(s.get(kBase)==nullptr);                                     // evicted
    CHECK(s.get(kBase+150)&&s.get(kBase+51)&&!s.get(kBase+50));       // window is exactly capacity
    CHECK(s.size()==100);
}

static HistoryInputs sample(int64_t utc,double indoor=21.0) {
    HistoryInputs in;
    in.clock_trusted=true;in.utc=utc;
    in.wifi_ok=in.pump_online=in.sensor_online=in.prices_ok=true;
    in.indoor_valid=true;in.indoor_c=indoor;
    in.price_valid=true;in.price_per_kwh=0.12345;
    in.action=2;
    return in;
}
constexpr int64_t T0=1800000000;    // a quarter-hour aligned instant (T0 % 900 == 0)

static void recorder_averages_and_gaps() {
    static_assert(T0%900==0,"aligned");
    HistoryStore store(2880);EventLog ev;HistoryRecorder rec_;
    // slot 0: three samples average to 21.0; no outdoor/power/rh -> stay missing
    CHECK(!rec_.observe(sample(T0+10,20.0),store,ev));
    CHECK(!rec_.observe(sample(T0+400,21.0),store,ev));
    CHECK(!rec_.observe(sample(T0+800,22.0),store,ev));
    CHECK(store.empty());                                    // the open slot is never stored early
    // slot 1 starts: slot 0 is finalized
    HistoryInputs in=sample(T0+900,25.0);
    in.outdoor_valid=true;in.outdoor_c=-3.0;in.outdoor_is_forecast=true;
    in.power_valid=true;in.power_w=400;in.setpoint_valid=true;in.setpoint_c=22.5;in.armed=true;
    CHECK(rec_.observe(in,store,ev));
    const HistoryRecord* r0=store.get(static_cast<uint32_t>(T0/900));
    CHECK(r0&&r0->indoor_c100==2100&&r0->outdoor_c100==kMissingI16&&r0->power_w==kMissingU16&&
          r0->setpoint_c10==kMissingI16&&r0->rh_c100==kMissingU16);
    CHECK(r0->price_e5==12345&&(r0->flags&kActionMask)==2&&!(r0->flags&kOutdoorIsForecast));
    // a long outage: nothing is recorded for the missing slots (gap, no fill)
    CHECK(rec_.observe(sample(T0+900*10+5,23.0),store,ev));     // finalizes slot 1
    CHECK(store.get(static_cast<uint32_t>(T0/900)+1)&&!store.get(static_cast<uint32_t>(T0/900)+2));
    const HistoryRecord* r1=store.get(static_cast<uint32_t>(T0/900)+1);
    CHECK(r1->indoor_c100==2500&&r1->outdoor_c100==-300&&(r1->flags&kOutdoorIsForecast)&&(r1->flags&kControlArmed));
    CHECK(r1->power_w==400&&r1->setpoint_c10==225);
}

static void forecast_provenance_survives_a_later_measurement() {
    // Codex P2 (#60): a forecast sample followed by a measured one averaged the two
    // but cleared the forecast flag, so the mixed value was shown as measured.
    HistoryStore store(2880);EventLog ev;HistoryRecorder rec_;
    HistoryInputs a=sample(T0+10,20.0);
    a.outdoor_valid=true;a.outdoor_c=10.0;a.outdoor_is_forecast=true;
    HistoryInputs b=sample(T0+500,20.0);
    b.outdoor_valid=true;b.outdoor_c=0.0;b.outdoor_is_forecast=false;
    CHECK(!rec_.observe(a,store,ev));CHECK(!rec_.observe(b,store,ev));
    CHECK(rec_.observe(sample(T0+910,20.0),store,ev));
    const HistoryRecord* r=store.get(static_cast<uint32_t>(T0/900));
    CHECK(r&&r->outdoor_c100==500&&(r->flags&kOutdoorIsForecast));
    // purely measured slots stay unflagged, and the flag does not leak to the next slot
    HistoryInputs m=sample(T0+920,20.0);m.outdoor_valid=true;m.outdoor_c=1.0;
    CHECK(!rec_.observe(m,store,ev));
    CHECK(rec_.observe(sample(T0+1810,20.0),store,ev));
    const HistoryRecord* r2=store.get(static_cast<uint32_t>(T0/900)+1);
    CHECK(r2&&r2->outdoor_c100==100&&!(r2->flags&kOutdoorIsForecast));
}

static void meter_scope_is_stored_with_each_record() {
    // Codex P1 (#60): the response labelled ALL retained history with the owner's
    // CURRENT scope, so switching meters relabelled old whole-house readings.
    HistoryStore store(2880);EventLog ev;HistoryRecorder rec_;
    const uint32_t s0=static_cast<uint32_t>(T0/900);
    auto power=[&](int64_t t,double w,uint8_t scope) {
        HistoryInputs in=sample(t,20.0);in.power_valid=true;in.power_w=w;in.power_scope=scope;return in;
    };
    rec_.observe(power(T0+10,1000,2),store,ev);                        // slot 0: whole house
    rec_.observe(power(T0+900+10,400,1),store,ev);                     // slot 1: pump only
    rec_.observe(power(T0+1800+10,300,1),store,ev);
    // a meter change INSIDE one slot cannot be averaged honestly
    rec_.observe(power(T0+2700+10,300,1),store,ev);                    // slot 3 starts, finalizes slot 2
    rec_.observe(power(T0+2700+500,2000,2),store,ev);
    rec_.observe(power(T0+3600+10,100,1),store,ev);                    // finalizes slot 3
    const HistoryRecord* r0=store.get(s0);const HistoryRecord* r1=store.get(s0+1);
    const HistoryRecord* r2=store.get(s0+2);const HistoryRecord* r3=store.get(s0+3);
    CHECK(r0&&r0->power_w==1000&&((r0->flags&kPowerScopeMask)>>kPowerScopeShift)==2);
    CHECK(r1&&r1->power_w==400&&((r1->flags&kPowerScopeMask)>>kPowerScopeShift)==1);
    CHECK(r2&&r2->power_w==300&&((r2->flags&kPowerScopeMask)>>kPowerScopeShift)==1);
    CHECK(r3&&r3->power_w==kMissingU16);                               // two targets in one slot: missing, not averaged
    CHECK(r3&&((r3->flags&kPowerScopeMask)>>kPowerScopeShift)==0);
    // queries label the range by what was measured, never by today's setting
    std::vector<HistoryPoint> pts(kMaxHistoryPoints);
    HistorySummary sum;
    const int64_t end=(static_cast<int64_t>(s0)+3)*900+100;
    size_t n=build_history_series(store,end+0,96,1,pts.data(),pts.size(),sum);
    std::string out;
    EventLog none;
    history_json("24h",1,pts.data(),n,sum,none,pts[0].t,"EUR","heat_pump",true,[&](const char* p){out+=p;});
    CHECK(out.find("\"meter_scope\":\"mixed\"")!=std::string::npos);       // whole house + pump only in one range
    CHECK(out.find("\"current_meter_scope\":\"heat_pump\"")!=std::string::npos);
    CHECK(out.find("\"kwh\":null")!=std::string::npos);                      // never summed across targets
    enevina::met_detail::Json v{out.data(),out.data()+out.size()};
    CHECK(v.value());
    // only whole-house readings in range: labelled whole house although the owner now says pump-only
    HistoryStore only;HistoryRecorder r2_;EventLog e2;
    // Each 15-minute energy slot needs actual consecutive meter coverage,
    // not one reading extrapolated across the entire quarter-hour.
    for(int sec=10;sec<900;sec+=15)
        r2_.observe(power(T0+sec,800,2),only,e2);
    for(int sec=10;sec<900;sec+=15)
        r2_.observe(power(T0+900+sec,800,2),only,e2);
    r2_.observe(power(T0+1800+10,800,2),only,e2);
    const int64_t end2=(static_cast<int64_t>(s0)+2)*900+100;
    n=build_history_series(only,end2,96,1,pts.data(),pts.size(),sum);
    std::string out2;
    history_json("24h",1,pts.data(),n,sum,e2,pts[0].t,"EUR","heat_pump",true,[&](const char* p){out2+=p;});
    CHECK(out2.find("\"meter_scope\":\"whole_house\"")!=std::string::npos);
    CHECK(out2.find("\"current_meter_scope\":\"heat_pump\"")!=std::string::npos);
    CHECK(out2.find("\"kwh\":null")==std::string::npos);
    // an unconfirmed meter is "unknown", and stays so
    HistoryStore unk;HistoryRecorder r3_;EventLog e3;
    r3_.observe(power(T0+10,500,0),unk,e3);
    r3_.observe(power(T0+900+10,500,0),unk,e3);
    n=build_history_series(unk,(static_cast<int64_t>(s0)+1)*900+100,96,1,pts.data(),pts.size(),sum);
    std::string out3;
    history_json("24h",1,pts.data(),n,sum,e3,pts[0].t,"EUR","whole_house",true,[&](const char* p){out3+=p;});
    CHECK(out3.find("\"meter_scope\":\"unknown\"")!=std::string::npos);
    // a corrupt scope code never becomes a claim
    HistoryInputs bad=power(T0+5000,100,3);
    HistoryStore bs;HistoryRecorder br;EventLog be;
    br.observe(bad,bs,be);
    HistoryInputs next=sample(T0+6000,20.0);
    br.observe(next,bs,be);
    const HistoryRecord* rb=bs.get(static_cast<uint32_t>((T0+5000)/900));
    CHECK(rb&&((rb->flags&kPowerScopeMask)>>kPowerScopeShift)==0);
}

static void recorder_clock_rules() {
    HistoryStore store(2880);EventLog ev;HistoryRecorder rec_;
    HistoryInputs in=sample(T0+10);
    in.clock_trusted=false;
    CHECK(!rec_.observe(in,store,ev));
    CHECK(ev.count()==0);                                         // no clock, no records, no events
    in.clock_trusted=true;in.utc=100;                             // 1970: clearly untrusted
    CHECK(!rec_.observe(in,store,ev)&&ev.count()==0);
    CHECK(!rec_.observe(sample(T0+10),store,ev));
    CHECK(!rec_.observe(sample(T0+100),store,ev));
    // clock jumps backwards by hours: ignored, nothing is corrupted
    CHECK(!rec_.observe(sample(T0-7200),store,ev));
    CHECK(!rec_.observe(sample(T0+200,30.0),store,ev));
    CHECK(rec_.observe(sample(T0+905),store,ev));
    const HistoryRecord* r=store.get(static_cast<uint32_t>(T0/900));
    CHECK(r&&r->indoor_c100>=2090&&r->indoor_c100<=3200);        // no sample from the bad clock leaked in
    // implausible values never enter a record
    HistoryStore s2(2880);EventLog e2;HistoryRecorder r2;
    HistoryInputs bad=sample(T0+10,500.0);
    bad.rh_valid=true;bad.rh_pct=140;bad.power_valid=true;bad.power_w=-5;
    r2.observe(bad,s2,e2);
    r2.observe(sample(T0+1000),s2,e2);
    const HistoryRecord* b=s2.get(static_cast<uint32_t>(T0/900));
    CHECK(b&&b->indoor_c100==kMissingI16&&b->rh_c100==kMissingU16&&b->power_w==kMissingU16);
}

static void events_debounce_and_commands() {
    HistoryStore store(2880);EventLog ev;HistoryRecorder rec_;
    int64_t t=T0;
    HistoryInputs in=sample(t);
    in.reset_reason=3;
    rec_.observe(in,store,ev);
    CHECK(ev.count()==1&&ev.newest(0).kind==static_cast<uint8_t>(HistoryEventKind::Boot)&&ev.newest(0).value==3);
    // pump data flaps for 30 s: no event
    t+=20;in=sample(t);in.pump_online=false;rec_.observe(in,store,ev);
    t+=30;in=sample(t);in.pump_online=true;rec_.observe(in,store,ev);
    t+=100;rec_.observe(sample(t),store,ev);
    CHECK(ev.count()==1);
    // pump data lost for 5 minutes: one Lost (at the loss time) and one Back with the duration
    const int64_t lost_at=t+10;
    for(int i=0;i<40;++i) {t+=10;in=sample(t);in.pump_online=false;rec_.observe(in,store,ev);}
    t+=10;rec_.observe(sample(t),store,ev);
    for(int i=0;i<10;++i) {t+=10;rec_.observe(sample(t),store,ev);}
    CHECK(ev.count()==3);
    CHECK(ev.newest(1).kind==static_cast<uint8_t>(HistoryEventKind::SourceLost)&&
          ev.newest(1).source==static_cast<uint8_t>(HistorySource::Pump));
    CHECK(std::llabs(static_cast<int64_t>(ev.newest(1).utc)-(lost_at+10))<=20);
    CHECK(ev.newest(0).kind==static_cast<uint8_t>(HistoryEventKind::SourceBack)&&
          ev.newest(0).utc==static_cast<uint32_t>(lost_at+400)&&
          ev.newest(0).value==400); // do not add recovery debounce duration
    // commands: counter deltas become events; a counter that resets (reboot) does not
    in=sample(t+10);in.commands_sent=2;in.commands_confirmed=1;rec_.observe(in,store,ev);
    CHECK(ev.count()==5&&ev.newest(1).kind==static_cast<uint8_t>(HistoryEventKind::CommandSent)&&ev.newest(1).value==2);
    CHECK(ev.newest(0).arg==0); // old counter-only observations stay anonymous
    in=sample(t+20);in.commands_sent=0;in.commands_confirmed=0;rec_.observe(in,store,ev);   // reset
    CHECK(ev.count()==5);
    in=sample(t+30);in.commands_failed=1;rec_.observe(in,store,ev);
    CHECK(ev.count()==6&&ev.newest(0).kind==static_cast<uint8_t>(HistoryEventKind::CommandFailed));
    // ring is bounded
    for(int i=0;i<300;++i) ev.put({static_cast<uint32_t>(T0+i),1,0,0});
    CHECK(ev.count()==kEventSlots&&ev.newest(0).utc==static_cast<uint32_t>(T0+299));
}

static void pump_cost_and_readback_evidence() {
    HistoryStore store(8);
    const uint32_t first=static_cast<uint32_t>(T0/900);
    const auto put=[&](unsigned offset,unsigned scope,unsigned minutes,
                       unsigned currency,int32_t price) {
        HistoryRecord r=rec(first+offset,12);
        r.power_w=1000;
        r.price_e5=price;
        r.flags=(scope<<kPowerScopeShift)|(minutes<<kPowerCoverageShift)|
                (currency<<kPriceCurrencyShift);
        CHECK(store.put(r));
    };
    put(0,1,15,1,20000); // 0.050 EUR
    put(1,1,6,1,-10000); // -0.010 EUR
    put(2,0,15,1,20000); // unknown scope
    put(3,2,15,1,20000); // house
    put(4,1,15,0,20000); // unknown currency
    put(5,1,15,1,kMissingI32);
    HistoryPoint points[8]{};
    HistorySummary summary;
    CHECK(build_history_series(store,T0+5*900,6,3,points,8,summary)==2);
    CHECK(std::fabs(summary.pump_cost.value()-0.04)<1e-9);
    CHECK(summary.pump_cost.minutes==21);

    HistoryRecorder recorder;
    EventLog events;
    recorder.observe(sample(T0+6*900),store,events);
    auto in=sample(T0+6*900+10);
    in.commands_confirmed=1;
    in.cn105_ack={static_cast<uint32_t>(in.utc-2),2,2,160};
    recorder.observe(in,store,events);
    CHECK(events.newest(0).utc==in.utc-2 && events.newest(0).arg!=0);
    MemFile record_file,event_file;
    HistoryPersistence persistence(record_file,event_file);
    CHECK(persistence.flush(store,events));
    HistoryStore restored_store(8);
    EventLog restored_events;
    HistoryPersistence restored(record_file,event_file);
    CHECK(restored.load(restored_store,restored_events).events==events.count());
    CHECK(restored_events.newest(0).arg==events.newest(0).arg);
    std::string json;
    history_json("24h",3,points,2,summary,restored_events,T0,"EUR","unknown",true,
                 [&](const char* part){json+=part;});
    CHECK(json.find("\"pump_cost\":0.04000")!=std::string::npos);
    CHECK(json.find("\"control_step\":\"HEAT\"")!=std::string::npos);
    CHECK(json.find("\"readback\":\"CN105_GATEWAY_GET\"")!=std::string::npos);
    CHECK(json.find("\"target_c\":16.0")!=std::string::npos);

    put(6,1,15,2,20000); // supported but different currency
    CHECK(build_history_series(store,T0+6*900,7,1,points,8,summary)==7);
    CHECK(std::isnan(summary.pump_cost.value()));
    CHECK(summary.pump_cost.minutes==36);
}

static void series_and_summary() {
    HistoryStore store(2880);
    const uint32_t first=static_cast<uint32_t>(T0/900);
    // 2 days of data with a 6 h hole; indoor 20 at night, 22 by day; power only some of the time
    for(uint32_t i=0;i<192;++i) {
        if(i>=40&&i<64) continue;
        HistoryRecord r=rec(first+i,(i%96)<48?20.0:22.0);
        if(i%2==0) r.power_w=400;
        r.outdoor_c100=-200;r.flags=2|kOutdoorIsForecast;
        // Synthetic complete 15-minute measurements explicitly carry full
        // verified coverage. Legacy records without it must NOT claim energy.
        if(i%2==0)r.flags|=static_cast<uint16_t>(15u<<kPowerCoverageShift);
        store.put(r);
    }
    const int64_t end=(static_cast<int64_t>(first)+191)*900+100;
    std::vector<HistoryPoint> pts(kMaxHistoryPoints);
    HistorySummary sum;
    CHECK(build_history_series(store,end,96,1,pts.data(),pts.size(),sum)==96);
    CHECK(pts[95].t==(static_cast<int64_t>(first)+191)*900&&pts[0].t==(static_cast<int64_t>(first)+96)*900);
    CHECK(sum.slots_total==96&&sum.slots_present==96&&std::fabs(sum.coverage()-1.0)<1e-9);
    // hourly buckets across the hole: the bucket in the hole has no records and no values
    CHECK(build_history_series(store,end,192,4,pts.data(),pts.size(),sum)==48);
    CHECK(sum.slots_present==192-24&&sum.slots_total==192);
    size_t empty_buckets=0;
    for(size_t i=0;i<48;++i) if(pts[i].records==0) {++empty_buckets;CHECK(std::isnan(pts[i].indoor)&&std::isnan(pts[i].outdoor));}
    CHECK(empty_buckets==6);                                        // 24 missing slots / 4 per bucket
    CHECK(std::fabs(sum.coverage()-168.0/192.0)<1e-9);
    // energy only from slots that had a reading: 84 slots x 0.4 kW x 0.25 h = 8.4 kWh
    CHECK(sum.kwh_slots>0&&std::fabs(sum.kwh-sum.kwh_slots*0.1)<1e-9);
    CHECK(sum.indoor.min==20.0&&sum.indoor.max==22.0&&std::fabs(sum.indoor.avg()-21.0)<0.1);
    // forecast provenance is carried to the point
    bool saw_forecast=false;
    for(size_t i=0;i<48;++i) saw_forecast|=pts[i].outdoor_forecast;
    CHECK(saw_forecast);
    // invalid requests
    CHECK(build_history_series(store,end,100,3,pts.data(),pts.size(),sum)==0);        // not a multiple
    CHECK(build_history_series(store,end,2880,1,pts.data(),kMaxHistoryPoints,sum)==0); // too many points
    CHECK(build_history_series(store,0,96,1,pts.data(),pts.size(),sum)==0);
    CHECK(build_history_series(store,end,5000,100,pts.data(),pts.size(),sum)==0);      // beyond capacity
    // range names
    RangeSpec rs{};
    CHECK(history_range_spec("24h",rs)&&rs.range_slots==96&&rs.step_slots==1);
    CHECK(history_range_spec("7d",rs)&&rs.range_slots/rs.step_slots==168);
    CHECK(history_range_spec("30d",rs)&&rs.range_slots/rs.step_slots==240&&rs.range_slots<=kHistorySlots);
    CHECK(!history_range_spec("1y",rs)&&!history_range_spec(nullptr,rs)&&!history_range_spec("",rs));
}

static void json_is_valid() {
    HistoryStore store(2880);EventLog ev;
    const uint32_t first=static_cast<uint32_t>(T0/900);
    for(uint32_t i=0;i<100;++i) {HistoryRecord r=rec(first+i);r.outdoor_c100=-150;store.put(r);}
    ev.put({static_cast<uint32_t>(T0+7200),static_cast<uint8_t>(HistoryEventKind::SourceLost),2,0});
    ev.put({static_cast<uint32_t>(T0-999999),1,0,5});                  // before the range: not listed
    std::vector<HistoryPoint> pts(kMaxHistoryPoints);
    HistorySummary sum;
    const int64_t end=(static_cast<int64_t>(first)+99)*900;
    const size_t n=build_history_series(store,end,96,1,pts.data(),pts.size(),sum);
    std::string out;
    history_json("24h",1,pts.data(),n,sum,ev,pts[0].t,"EUR","unknown",true,[&](const char* p){out+=p;});
    enevina::met_detail::Json v{out.data(),out.data()+out.size()};
    CHECK(v.value());v.ws();CHECK(v.cur==v.end);
    CHECK(out.find("nan")==std::string::npos&&out.find("inf")==std::string::npos);
    CHECK(out.find("\"valid\":true")!=std::string::npos&&out.find("\"meter_scope\":\"unknown\"")!=std::string::npos);
    CHECK(out.find("\"persistent\":true")!=std::string::npos);
    CHECK(out.find("\"kwh\":null")!=std::string::npos);               // no power readings: energy is unknown, not 0
    CHECK(out.find("\"rh\":null")!=std::string::npos&&out.find("\"sp\":null")!=std::string::npos);
    CHECK(out.find("\"k\":2")!=std::string::npos&&out.find("\"v\":5")==std::string::npos);
    CHECK(out.size()<20000);
    std::string empty;
    HistoryStore none(2880);
    HistorySummary s0;
    history_json("24h",1,pts.data(),0,s0,ev,0,"EUR","unknown",false,[&](const char* p){empty+=p;});
    enevina::met_detail::Json v2{empty.data(),empty.data()+empty.size()};
    CHECK(v2.value()&&empty.find("\"valid\":false")!=std::string::npos);
}

static void persistence_roundtrip_and_wear() {
    MemFile rf,ef;
    HistoryPersistence p(rf,ef);
    HistoryStore store(2880);EventLog ev;
    const uint32_t first=static_cast<uint32_t>(T0/900);
    auto load0=p.load(store,ev);
    CHECK(!load0.record_header_ok&&store.empty()&&p.needs_repair());        // fresh device: file laid down on first flush
    for(uint32_t i=0;i<4;++i) store.put(rec(first+i,20.0+i));
    ev.put({static_cast<uint32_t>(T0),1,0,7});
    CHECK(p.flush(store,ev)&&rf.replaces==1&&p.file_records()==4&&!p.needs_repair());
    // hourly flushes append only what is new
    for(uint32_t i=4;i<8;++i) store.put(rec(first+i,20.0+i));
    CHECK(p.flush(store,ev)&&rf.appends==1&&rf.replaces==1&&p.file_records()==8);
    CHECK(rf.data.size()==8+8*22);
    // nothing new: no write at all
    const size_t before=rf.bytes_written;
    CHECK(p.flush(store,ev)&&rf.bytes_written==before);
    // restart: everything comes back exactly
    HistoryStore s2(2880);EventLog e2;MemFile rf2=rf,ef2=ef;
    HistoryPersistence p2(rf2,ef2);
    auto l=p2.load(s2,e2);
    CHECK(l.record_header_ok&&l.records==8&&!l.record_tail_cut&&l.events==1&&e2.newest(0).value==7);
    for(uint32_t i=0;i<8;++i) {const HistoryRecord* r=s2.get(first+i);CHECK(r&&r->indoor_c100==2000+static_cast<int>(i)*100);}
    CHECK(!p2.needs_repair());
    // the loaded file keeps appending (no rewrite)
    s2.put(rec(first+8));
    CHECK(p2.flush(s2,e2)&&rf2.replaces==rf.replaces&&rf2.appends==rf.appends+1&&rf2.data.size()==8+9*22);
}

static void torn_and_corrupt_files() {
    MemFile rf,ef;
    HistoryPersistence p(rf,ef);
    HistoryStore store(2880);EventLog ev;
    const uint32_t first=static_cast<uint32_t>(T0/900);
    p.load(store,ev);
    for(uint32_t i=0;i<6;++i) store.put(rec(first+i));
    CHECK(p.flush(store,ev));
    // a power cut during an append leaves half a batch
    for(uint32_t i=6;i<10;++i) store.put(rec(first+i));
    rf.torn_append=true;
    CHECK(!p.flush(store,ev)&&p.needs_repair());
    rf.torn_append=false;
    // restart now: the valid prefix is kept, the torn tail is cut, repair is scheduled
    {
        HistoryStore s2(2880);EventLog e2;MemFile rf2=rf,ef2=ef;
        HistoryPersistence p2(rf2,ef2);
        auto l=p2.load(s2,e2);
        CHECK(l.records>=6&&l.records<=8&&l.record_tail_cut&&p2.needs_repair());
        CHECK(s2.get(first)&&s2.get(first+5));
        s2.put(rec(first+20));
        CHECK(p2.flush(s2,e2)&&rf2.replaces==rf.replaces+1);                  // repaired by an atomic rewrite, never appended after a torn tail
        HistoryStore s3(2880);EventLog e3;MemFile rf3=rf2,ef3=ef2;HistoryPersistence p3(rf3,ef3);
        auto l3=p3.load(s3,e3);
        CHECK(!l3.record_tail_cut&&s3.get(first+20)&&s3.get(first+5));
    }
    // without a restart the next flush repairs the file too
    CHECK(p.flush(store,ev)&&rf.replaces==2&&!p.needs_repair());
    {
        HistoryStore s2(2880);EventLog e2;MemFile rf2=rf,ef2=ef;HistoryPersistence p2(rf2,ef2);
        auto l=p2.load(s2,e2);
        CHECK(l.records==10&&!l.record_tail_cut);
    }
    // a flipped byte in the middle: records before it survive, the rest is dropped and repaired
    {
        MemFile bad=rf;
        bad.data[8+3*22+5]^=0x40;
        HistoryStore s2(2880);EventLog e2;MemFile e_copy=ef;HistoryPersistence p2(bad,e_copy);
        auto l=p2.load(s2,e2);
        CHECK(l.records==3&&l.record_tail_cut&&s2.get(first+2)&&!s2.get(first+3));
    }
    // foreign / old-version / empty files are replaced, never interpreted
    for(int variant=0;variant<4;++variant) {
        MemFile weird;weird.present=true;
        if(variant==0) weird.data.assign(100,0xAB);
        if(variant==1) {weird.data=rf.data;weird.data[4]=9;}                 // version 9
        if(variant==2) {weird.data=rf.data;weird.data[6]=99;}                // wrong record size
        if(variant==3) weird.data.clear();
        HistoryStore s2(2880);EventLog e2;MemFile ec;HistoryPersistence p2(weird,ec);
        auto l=p2.load(s2,e2);
        CHECK(!l.record_header_ok&&s2.empty()&&p2.needs_repair());
    }
    // duplicate / backwards slots end the load
    {
        MemFile dup=rf;
        // copy record #2 over record #4 -> slot goes backwards at #4
        std::memcpy(dup.data.data()+8+4*22,dup.data.data()+8+2*22,22);
        HistoryStore s2(2880);EventLog e2;MemFile ec=ef;HistoryPersistence p2(dup,ec);
        auto l=p2.load(s2,e2);
        CHECK(l.records==4&&l.record_tail_cut);
    }
    // append and replace failures never advance the persisted position
    {
        MemFile r2,e2f;HistoryPersistence p2(r2,e2f);HistoryStore s;EventLog e;
        p2.load(s,e);
        s.put(rec(first));
        r2.fail_replace=true;
        CHECK(!p2.flush(s,e)&&p2.needs_repair());
        r2.fail_replace=false;
        CHECK(p2.flush(s,e)&&!p2.needs_repair()&&p2.file_records()==1);
        s.put(rec(first+1));
        r2.fail_append=true;
        CHECK(!p2.flush(s,e)&&p2.needs_repair());
        r2.fail_append=false;
        CHECK(p2.flush(s,e)&&p2.file_records()==2&&r2.data.size()==8+2*22);
    }
}

static void bounded_file_growth_and_compaction() {
    MemFile rf,ef;
    HistoryPersistence p(rf,ef);
    HistoryStore store(2880);EventLog ev;
    p.load(store,ev);
    const uint32_t first=static_cast<uint32_t>(T0/900);
    // 120 days, flushed every 4 slots (hourly): the file never grows past ring + slack
    size_t max_records=0;
    for(uint32_t i=0;i<120*96;++i) {
        store.put(rec(first+i));
        if(i%4==3) {CHECK(p.flush(store,ev));max_records=std::max(max_records,p.file_records());}
    }
    CHECK(max_records<=2880+kHistoryCompactSlack+4);
    CHECK(rf.data.size()<=8+(2880+kHistoryCompactSlack+4)*22);
    // compaction happens rarely: 120 days of writes = appends + a handful of rewrites
    CHECK(rf.replaces<=1+(120*96)/kHistoryCompactSlack);
    // total flash traffic per day stays small (record bytes + occasional rewrite), far below the partition
    const double per_day=static_cast<double>(rf.bytes_written)/120.0;
    CHECK(per_day<2880.0*22/ (kHistoryCompactSlack/96.0) *1.2);       // one full rewrite per 2.5 days at most
    CHECK(per_day<30000.0);
    // reload gives exactly the last 30 days
    HistoryStore s2(2880);EventLog e2;MemFile rf2=rf,ef2=ef;HistoryPersistence p2(rf2,ef2);
    auto l=p2.load(s2,e2);
    CHECK(l.records>=2880&&s2.newest_slot()==first+120*96-1&&s2.size()==2880);
}


static void update_before_clock_sync() {
    HistoryStore store(2880);EventLog ev;HistoryRecorder recorder;
    auto in=sample(T0);
    in.clock_trusted=false;in.utc=0;in.ota_good_version=20261101;
    CHECK(!recorder.observe(in,store,ev)&&ev.count()==0);
    // Signed OTA confirms the new version before NTP becomes available.
    in.ota_good_version=20261119;
    CHECK(!recorder.observe(in,store,ev)&&ev.count()==0);
    in=sample(T0+900);in.ota_good_version=20261119;
    recorder.observe(in,store,ev);
    CHECK(ev.count()==2); // trusted Boot and deferred UpdateConfirmed
    CHECK(ev.newest(0).kind==static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed));
    CHECK(ev.newest(0).arg==20261119 && ev.newest(0).utc==T0+900);
    in=sample(T0+915);in.ota_good_version=20261119;
    recorder.observe(in,store,ev);
    CHECK(ev.count()==2); // no duplicated confirmation
    // A rollback before clock synchronization must cancel the pending success.
    HistoryStore s2(2880);EventLog e2;HistoryRecorder r2;
    in.clock_trusted=false;in.utc=0;in.ota_good_version=20261101;
    r2.observe(in,s2,e2);
    in.ota_good_version=20261119;r2.observe(in,s2,e2);
    in.ota_good_version=20261101;r2.observe(in,s2,e2);
    in=sample(T0+900);in.ota_good_version=20261101;r2.observe(in,s2,e2);
    CHECK(e2.count()==1);
}

static void update_confirmation_events() {
    HistoryStore store(2880);EventLog ev;HistoryRecorder rec_;
    int64_t t=T0;
    HistoryInputs in=sample(t);
    in.ota_good_version=20261101;
    rec_.observe(in,store,ev);
    CHECK(ev.count()==1);                                     // boot only: a normal reboot is not an update
    in=sample(t+15);in.ota_good_version=20261101;rec_.observe(in,store,ev);
    CHECK(ev.count()==1);
    // the signed OTA confirms the new version after the boot health check
    in=sample(t+400);in.ota_good_version=20261119;rec_.observe(in,store,ev);
    CHECK(ev.count()==2);
    CHECK(ev.newest(0).kind==static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed)&&ev.newest(0).arg==20261119);
    CHECK(ev.newest(0).utc==static_cast<uint32_t>(t+400));
    in=sample(t+500);in.ota_good_version=20261119;rec_.observe(in,store,ev);
    CHECK(ev.count()==2);                                     // not repeated
    // an unknown (0) reading never logs, and re-bases so the next real value is compared fairly
    in=sample(t+600);in.ota_good_version=0;rec_.observe(in,store,ev);
    CHECK(ev.count()==2);
    in=sample(t+700);in.ota_good_version=20261119;rec_.observe(in,store,ev);
    CHECK(ev.count()==2);                                     // 0 is "unknown", never a baseline: no false update
    // a boot where the OTA store is read late (0 first, then the stored version) is not an update
    HistoryStore sb(2880);EventLog eb;HistoryRecorder rb;
    in=sample(T0+10);in.ota_good_version=0;rb.observe(in,sb,eb);
    in=sample(T0+25);in.ota_good_version=20261101;rb.observe(in,sb,eb);
    CHECK(eb.count()==1);
    in=sample(T0+40);in.ota_good_version=20261119;rb.observe(in,sb,eb);
    CHECK(eb.count()==2&&eb.newest(0).arg==20261119);
    // the 32-bit argument survives flash and reload
    MemFile rf,ef;HistoryPersistence p(rf,ef);
    HistoryStore s2(2880);EventLog e2;
    p.load(s2,e2);
    s2.put(rec(static_cast<uint32_t>(T0/900)));
    CHECK(p.flush(s2,ev));
    HistoryStore s3(2880);EventLog e3;MemFile rf3=rf,ef3=ef;HistoryPersistence p3(rf3,ef3);
    CHECK(p3.load(s3,e3).events==ev.count());
    bool found=false;
    for(size_t i=0;i<e3.count();++i) found|=e3.newest(i).kind==static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed)&&e3.newest(i).arg==20261119;
    CHECK(found);
    // JSON carries it as "a"
    std::string out;
    std::vector<HistoryPoint> pts(kMaxHistoryPoints);HistorySummary sum;
    HistoryStore s4(2880);
    for(uint32_t i=0;i<100;++i) s4.put(rec(static_cast<uint32_t>(T0/900)+i));
    const size_t n=build_history_series(s4,(T0/900+99)*900,96,1,pts.data(),pts.size(),sum);
    history_json("24h",1,pts.data(),n,sum,ev,pts[0].t-100000,"EUR","unknown",true,[&](const char* part){out+=part;});
    CHECK(out.find("\"k\":7")!=std::string::npos&&out.find("\"a\":20261119")!=std::string::npos);
}

static void ota_update_summary_survives_rotating_history() {
    using enevina::cottage::make_ota_history_last;
    using enevina::cottage::valid_ota_history_last;
    const auto summary=make_ota_history_last(20261119,T0+900);
    CHECK(valid_ota_history_last(summary));
    auto invalid=summary;invalid.utc=0;
    CHECK(!valid_ota_history_last(invalid));
    invalid=summary;invalid.version=0;
    CHECK(!valid_ota_history_last(invalid));
    invalid=summary;invalid.seal^=1;
    CHECK(!valid_ota_history_last(invalid));
    EventLog events;
    events.put({static_cast<uint32_t>(summary.utc),
      static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed),0,0,summary.version});
    for(size_t i=0;i<kEventSlots+25;++i)
        events.put({static_cast<uint32_t>(summary.utc+i+1),
             static_cast<uint8_t>(HistoryEventKind::Boot),0,0,0});
    bool found=false;
    for(size_t i=0;i<events.count();++i)
        found|=events.newest(i).kind==static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed);
    CHECK(!found);
    CHECK(valid_ota_history_last(summary)&&summary.version==20261119&&
          summary.utc==T0+900); // independent of evictions and reboots
}
static void ota_history_ack_survives_event_ring_eviction() {
    using enevina::cottage::ota_pending_history_upgrade;
    CHECK(ota_pending_history_upgrade(20261119,20261119,0)==20261119);
    CHECK(ota_pending_history_upgrade(20261119,20261119,20261119)==0);
    CHECK(ota_pending_history_upgrade(20261101,20261119,0)==0);
    CHECK(ota_pending_history_upgrade(0,20261119,0)==0);
    CHECK(ota_pending_history_upgrade(20261119,0,0)==0);
    CHECK(ota_pending_history_upgrade(20261120,20261120,20261119)==20261120);
    HistoryStore store(20);EventLog events;HistoryRecorder original;
    auto in=sample(T0);in.ota_good_version=20261119;
    in.ota_upgrade_event_version=20261119;
    original.observe(in,store,events);
    CHECK(events.count()==2);
    for(size_t i=0;i<kEventSlots+1;i++)
        events.put({static_cast<uint32_t>(T0+100+i),
                   static_cast<uint8_t>(HistoryEventKind::Boot),0,0,0});
    unsigned old_events=0;
    for(size_t i=0;i<events.count();++i)
        old_events+=events.newest(i).kind==
            static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed);
    CHECK(old_events==0); // verified update event has been evicted
    HistoryRecorder restarted;
    in=sample(T0+900);in.ota_good_version=20261119;
    // The durable ack suppresses the already recorded event despite eviction.
    in.ota_upgrade_event_version=ota_pending_history_upgrade(
        20261119,20261119,20261119);
    restarted.observe(in,store,events);
    unsigned repeated=0;
    for(size_t i=0;i<events.count();++i)
        repeated+=events.newest(i).kind==
            static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed);
    CHECK(repeated==0);
}
static void ota_confirmed_before_first_history_sample_is_recorded() {
    HistoryStore s;EventLog e;HistoryRecorder rec_;
    auto in=sample(T0);in.ota_good_version=20261119;
    in.ota_upgrade_event_version=20261119;
    rec_.observe(in,s,e);
    CHECK(e.count()==2);
    CHECK(e.newest(0).kind==static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed));
    CHECK(e.newest(0).arg==20261119);
    rec_.observe(sample(T0+15),s,e);
    CHECK(e.count()==2);
    // Persist and reload the history while the signed-OTA NVS hint remains.
    MemFile records,events;HistoryPersistence p(records,events);
    CHECK(p.flush(s,e));
    HistoryStore restored;EventLog old_events;HistoryPersistence pr(records,events);
    const auto loaded=pr.load(restored,old_events);
    CHECK(loaded.events==2);
    HistoryRecorder next_boot;
    in=sample(T0+900);in.ota_good_version=20261119;
    in.ota_upgrade_event_version=20261119;
    next_boot.observe(in,restored,old_events);
    unsigned confirmations=0;
    for(size_t i=0;i<old_events.count();++i)
        confirmations+=old_events.newest(i).kind==
            static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed);
    CHECK(confirmations==1); // no duplicate across reboot
    // If NVS had no prior good release, a first installed version is
    // NOT an upgrade and has no signed-OTA event hint.
    HistoryStore fresh;EventLog baseline;HistoryRecorder first;
    in=sample(T0+10);in.ota_good_version=20261119;
    in.ota_upgrade_event_version=0;
    first.observe(in,fresh,baseline);
    CHECK(baseline.count()==1); // Boot only
    // A future/stale NVS hint cannot falsely claim a rollback was success.
    HistoryStore rolled;EventLog rb;HistoryRecorder rr;
    in=sample(T0+10);in.ota_good_version=20261101;
    in.ota_upgrade_event_version=20261119;
    rr.observe(in,rolled,rb);
    CHECK(rb.count()==1);
    // No trusted time until after successful OTA: defer, then log exactly once.
    HistoryStore s3;EventLog ev3;HistoryRecorder r3;
    in=sample(T0);in.clock_trusted=false;in.utc=0;
    in.ota_good_version=20261119;in.ota_upgrade_event_version=20261119;
    r3.observe(in,s3,ev3);
    CHECK(ev3.count()==0);
    in.clock_trusted=true;in.utc=T0+900;
    r3.observe(in,s3,ev3);
    CHECK(ev3.count()==2&&ev3.newest(0).arg==20261119);
}
static HistoryInputs meter_reading(int64_t t,double w,uint8_t scope) {
    auto in=sample(t);in.power_valid=true;in.power_w=w;in.power_scope=scope;return in;
}
static void measured_energy_requires_verifiable_coverage() {
    HistoryStore short_store;EventLog ev;HistoryRecorder recorder;
    // Meter first appears with 10 seconds left in the 15-minute slot.
    recorder.observe(meter_reading(T0+14*60+50,1000,1),short_store,ev);
    recorder.observe(sample(T0+900+5),short_store,ev);
    const auto* short_slot=short_store.get(static_cast<uint32_t>(T0/900));
    CHECK(short_slot&&short_slot->power_w==1000);
    CHECK(short_slot&&history_power_coverage_minutes(short_slot->flags)==0);
    std::vector<HistoryPoint> pts(kMaxHistoryPoints);HistorySummary sum;
    const auto n=build_history_series(short_store,T0+900+5,96,1,pts.data(),pts.size(),sum);
    CHECK(n==96&&sum.kwh_slots==0&&sum.verified_power_minutes==0);
    std::string js;history_json("24h",1,pts.data(),n,sum,ev,T0,"EUR",
        "heat_pump",true,[&](const char* b){js+=b;});
    CHECK(js.find("\"kwh\":null")!=std::string::npos);
    CHECK(js.find("\"power_coverage_minutes\":0")!=std::string::npos);
    // A single Shelly capture repeatedly read by a 15 s history worker is
    // still ONE physical sample. It cannot generate fake covered minutes.
    HistoryStore repeats;HistoryRecorder repeat_rec;EventLog repeat_events;
    for(int64_t tick=T0+850;tick<T0+900;tick+=10) {
        auto in=meter_reading(tick,1000,1);
        in.power_sample_utc=T0+850; // same unchanged Shelly capture
        repeat_rec.observe(in,repeats,repeat_events);
    }
    repeat_rec.observe(sample(T0+905),repeats,repeat_events);
    const auto* one=repeats.get(static_cast<uint32_t>(T0/900));
    CHECK(one&&history_power_coverage_minutes(one->flags)==0);
    build_history_series(repeats,T0+905,96,1,pts.data(),pts.size(),sum);
    CHECK(sum.kwh_slots==0&&sum.verified_power_minutes==0);
    // With 15-second consecutive reports the integral counts ONLY observed
    // intervals and never assumes 900 seconds from the first sample alone.
    HistoryStore covered_store;HistoryRecorder covered;EventLog ev2;
    for(int sec=10;sec<=880;sec+=15)
        covered.observe(meter_reading(T0+sec,1000,1),covered_store,ev2);
    covered.observe(sample(T0+900+5),covered_store,ev2);
    const auto* full=covered_store.get(static_cast<uint32_t>(T0/900));
    CHECK(full&&history_power_coverage_minutes(full->flags)==14);
    build_history_series(covered_store,T0+900+5,96,1,pts.data(),pts.size(),sum);
    CHECK(sum.kwh_slots==1&&sum.verified_power_minutes==14);
    CHECK(std::fabs(sum.kwh-14.0/60.0)<1e-6);
    // A long gap must not be bridged and counted as covered time.
    HistoryStore gap_store;HistoryRecorder gaps;EventLog ev3;
    gaps.observe(meter_reading(T0+10,1000,1),gap_store,ev3);
    gaps.observe(meter_reading(T0+850,1000,1),gap_store,ev3);
    gaps.observe(sample(T0+900+5),gap_store,ev3);
    const auto* gap=gap_store.get(static_cast<uint32_t>(T0/900));
    CHECK(gap&&history_power_coverage_minutes(gap->flags)==0);
}
static void historical_prices_retain_currency_without_cross_currency_mean() {
    CHECK(history_currency_id("EUR")==1&&history_currency_id("SEK")==2);
    CHECK(history_currency_id("NOK")==3&&history_currency_id("DKK")==4);
    CHECK(history_currency_id("PLN")==5&&history_currency_id("XYZ")==0);
    HistoryStore s;
    const uint32_t slot=static_cast<uint32_t>(T0/900);
    auto a=rec(slot);a.price_e5=10000;
    a.flags=static_cast<uint16_t>(history_currency_id("SEK")<<kPriceCurrencyShift);
    auto b=rec(slot+1);b.price_e5=30000;
    b.flags=static_cast<uint16_t>(history_currency_id("EUR")<<kPriceCurrencyShift);
    CHECK(s.put(a)&&s.put(b));
    HistorySummary sum;std::vector<HistoryPoint> pts(kMaxHistoryPoints);
    const auto n=build_history_series(s,T0+1800+1,96,1,pts.data(),pts.size(),sum);
    CHECK(n==96);
    CHECK((sum.currency_mask&(sum.currency_mask-1))!=0);
    CHECK(std::isnan(sum.price.avg())&&std::string(history_detail::currency_of_range(sum))=="mixed");
    std::string js;EventLog ev;
    history_json("24h",1,pts.data(),n,sum,ev,T0,"EUR","unknown",true,
        [&](const char* p){js+=p;});
    CHECK(js.find("\"currency\":\"mixed\"")!=std::string::npos);
    CHECK(js.find("\"current_currency\":\"EUR\"")!=std::string::npos);
    CHECK(js.find("\"pc\":\"SEK\"")!=std::string::npos);
    CHECK(js.find("\"pc\":\"EUR\"")!=std::string::npos);
    CHECK(js.find("\"price\":{\"min\":null,\"max\":null,\"avg\":null}")!=std::string::npos);
    build_history_series(s,T0+1800+1,96,4,pts.data(),pts.size(),sum);
    CHECK(std::isnan(pts.back().price) || std::isnan(pts[0].price));
    // Old format had no currency code. Don't retrospectively label it EUR.
    HistoryStore old;auto legacy=rec(slot);CHECK(old.put(legacy));
    build_history_series(old,T0+900+1,96,1,pts.data(),pts.size(),sum);
    CHECK(std::string(history_detail::currency_of_range(sum))=="unknown");
}


static void legacy_event_log_migration_preserves_old_entries() {
    using namespace persist_detail;
    MemFile records,legacy_events;
    legacy_events.present=true;
    const Header h=header(kEvtMagic,kLegacyEvtDisk);
    const auto* hp=reinterpret_cast<const uint8_t*>(&h);
    legacy_events.data.insert(legacy_events.data.end(),hp,hp+sizeof(h));
    const LegacyHistoryEvent previous[]={
        {static_cast<uint32_t>(T0),1,0,3},
        {static_cast<uint32_t>(T0+60),2,1,0},
        {static_cast<uint32_t>(T0+360),3,1,300},
        {static_cast<uint32_t>(T0+720),4,0,2}
    };
    for(const auto& e:previous) {
        const auto* p=reinterpret_cast<const uint8_t*>(&e);
        legacy_events.data.insert(legacy_events.data.end(),p,p+sizeof(e));
        const uint16_t crc=history_crc16(p,sizeof(e));
        const auto* c=reinterpret_cast<const uint8_t*>(&crc);
        legacy_events.data.insert(legacy_events.data.end(),c,c+sizeof(crc));
    }
    CHECK(legacy_events.data.size()==sizeof(Header)+sizeof(previous)/sizeof(previous[0])*kLegacyEvtDisk);
    HistoryStore store;EventLog events;
    HistoryPersistence migration(records,legacy_events);
    const auto loaded=migration.load(store,events);
    CHECK(loaded.event_header_ok && !loaded.event_tail_cut && loaded.events==4);
    CHECK(events.count()==4);
    CHECK(events.newest(0).utc==T0+720 && events.newest(0).kind==4 && events.newest(0).value==2);
    CHECK(events.newest(1).utc==T0+360 && events.newest(1).value==300);
    CHECK(events.newest(3).utc==T0 && events.newest(3).arg==0);
    // Even with no newly appended events, load must trigger atomic rewrite
    // of legacy events to the current format. No data is silently discarded.
    CHECK(migration.flush(store,events));
    Header migrated{};
    std::memcpy(&migrated,legacy_events.data.data(),sizeof(migrated));
    CHECK(migrated.record_size==kEvtDisk);
    CHECK(legacy_events.replaces>0);
    HistoryStore reread;EventLog old_log;
    HistoryPersistence restored(records,legacy_events);
    const auto after=restored.load(reread,old_log);
    CHECK(after.event_header_ok && after.events==4 && !after.event_tail_cut);
    CHECK(old_log.newest(0).utc==T0+720 && old_log.newest(0).arg==0);
    CHECK(old_log.newest(1).value==300);
    // A torn old-format file still returns the valid prefix and migrates it.
    MemFile truncated=legacy_events;truncated.data.clear();
    truncated.data.insert(truncated.data.end(),hp,hp+sizeof(h));
    const auto* p=reinterpret_cast<const uint8_t*>(&previous[0]);
    truncated.data.insert(truncated.data.end(),p,p+sizeof(previous[0]));
    uint16_t crc=history_crc16(p,sizeof(previous[0]));
    const auto* cp=reinterpret_cast<const uint8_t*>(&crc);
    truncated.data.insert(truncated.data.end(),cp,cp+2);
    truncated.data.push_back(0xAA);
    HistoryStore sr;EventLog er;HistoryPersistence pr(records,truncated);
    const auto r=pr.load(sr,er);
    CHECK(r.event_header_ok&&r.event_tail_cut&&r.events==1);
    CHECK(pr.flush(sr,er));
    CHECK(er.newest(0).utc==T0);
}

static void room_temperature_history_preserves_the_source_change() {
    HistoryStore store;
    EventLog events;
    HistoryRecorder recorder;
    HistoryInputs in{};
    in.clock_trusted=true;
    in.utc=T0;in.indoor_valid=true;in.indoor_c=19;
    in.indoor_source_id=1;
    recorder.observe(in,store,events);
    in.utc=T0+300;
    in.indoor_c=22;in.indoor_from_pump=true;in.indoor_correction_c100=-50;
    recorder.observe(in,store,events);
    CHECK(events.newest(0).kind==static_cast<uint8_t>(HistoryEventKind::RoomTemperatureSourceChanged));
    CHECK(events.newest(0).arg==1 && events.newest(0).value==-50);
    in.utc=T0+900;
    CHECK(recorder.observe(in,store,events));
    CHECK(store.get(T0/900)->indoor_c100==kMissingI16);
    in.utc=T0+1800;
    CHECK(recorder.observe(in,store,events));
    CHECK(store.get(T0/900+1)->indoor_c100==2200);
}

int main() {
    room_temperature_history_preserves_the_source_change();
    legacy_event_log_migration_preserves_old_entries();
    mount_failure_never_formats_nonblank_partition();
    measured_energy_requires_verifiable_coverage();
    historical_prices_retain_currency_without_cross_currency_mean();
    ring_semantics();
    recorder_averages_and_gaps();
    forecast_provenance_survives_a_later_measurement();
    meter_scope_is_stored_with_each_record();
    recorder_clock_rules();
    events_debounce_and_commands();
    pump_cost_and_readback_evidence();
    series_and_summary();
    json_is_valid();
    persistence_roundtrip_and_wear();
    torn_and_corrupt_files();
    bounded_file_growth_and_compaction();
    update_before_clock_sync();
    update_confirmation_events();
    ota_update_summary_survives_rotating_history();
    ota_history_ack_survives_event_ring_eviction();
    ota_confirmed_before_first_history_sample_is_recorded();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("history_tests OK");
    return 0;
}
