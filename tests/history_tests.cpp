// History ring, recorder, range queries and flash persistence (torn writes,
// corruption, compaction, wear bounds). Memory-backed files stand in for LittleFS.
#include "cottage/history_persist.hpp"
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
          ev.newest(0).value>=380&&ev.newest(0).value<=460);
    // commands: counter deltas become events; a counter that resets (reboot) does not
    in=sample(t+10);in.commands_sent=2;in.commands_confirmed=1;rec_.observe(in,store,ev);
    CHECK(ev.count()==5&&ev.newest(1).kind==static_cast<uint8_t>(HistoryEventKind::CommandSent)&&ev.newest(1).value==2);
    in=sample(t+20);in.commands_sent=0;in.commands_confirmed=0;rec_.observe(in,store,ev);   // reset
    CHECK(ev.count()==5);
    in=sample(t+30);in.commands_failed=1;rec_.observe(in,store,ev);
    CHECK(ev.count()==6&&ev.newest(0).kind==static_cast<uint8_t>(HistoryEventKind::CommandFailed));
    // ring is bounded
    for(int i=0;i<300;++i) ev.put({static_cast<uint32_t>(T0+i),1,0,0});
    CHECK(ev.count()==kEventSlots&&ev.newest(0).utc==static_cast<uint32_t>(T0+299));
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

int main() {
    ring_semantics();
    recorder_averages_and_gaps();
    recorder_clock_rules();
    events_debounce_and_commands();
    series_and_summary();
    json_is_valid();
    persistence_roundtrip_and_wear();
    torn_and_corrupt_files();
    bounded_file_growth_and_compaction();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("history_tests OK");
    return 0;
}
