// RuuviTag discovery list and sensor names, using the official Ruuvi Data
// Format 5 test vector (CB:B8:33:4C:88:4F, 24.3 C) with varied MAC/RSSI.
#include "cottage/ruuvi_registry.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace enevina::cottage;
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)

static std::vector<uint8_t> vector_frame() {
    static const char* hex="99040512FC5394C37C0004FFFC040CAC364200CDCBB8334C884F";
    std::vector<uint8_t> out;
    for(size_t i=0;hex[i]&&hex[i+1];i+=2) out.push_back(static_cast<uint8_t>(std::stoi(std::string(hex+i,2),nullptr,16)));
    return out;
}
static RuuviV5 sensor(uint8_t last_mac_byte,int rssi,double temp=0,double rh=-1) {
    auto f=vector_frame();
    f[25]=last_mac_byte;                       // vary the MAC's last octet
    RuuviV5 s{};
    CHECK(decode_ruuvi_v5(f.data(),f.size(),rssi,s));
    if(temp!=0) s.temperature_c=temp;
    if(rh>=0) s.relative_humidity_pct=rh;
    return s;
}

static void registry_lists_and_ages() {
    RuuviRegistry r;
    CHECK(r.observe(sensor(0x01,-60),1000));
    CHECK(r.observe(sensor(0x02,-45),2000));
    CHECK(r.observe(sensor(0x03,-80),3000));
    CHECK(r.observe(sensor(0x01,-58),4000));        // same MAC: updated, not duplicated
    CHECK(r.used_count()==3);
    RuuviSeen out[8];
    size_t n=r.snapshot(5000,out,8);
    CHECK(n==3);
    CHECK(out[0].latest.mac[5]==0x02&&out[1].latest.mac[5]==0x01&&out[2].latest.mac[5]==0x03);   // strongest first
    CHECK(out[1].frames==2&&out[1].latest.rssi_dbm==-58&&out[1].first_ms==1000&&out[1].last_ms==4000);
    CHECK(std::fabs(out[0].latest.temperature_c-24.3)<1e-6);                // the official vector
    // A sensor that stops advertising keeps its age and is forgotten after 15 min.
    n=r.snapshot(4000+kRuuviForgetMs,out,8);
    CHECK(n==1&&out[0].latest.mac[5]==0x01);                                // only the last frame is within 15 min
    n=r.snapshot(4000+kRuuviForgetMs+1,out,8);
    CHECK(n==0);
    // A clock that moved backwards never makes an entry look new.
    n=r.snapshot(500,out,8);
    CHECK(n==0);
    CHECK(r.snapshot(5000,out,2)==2);                                       // honours capacity
}

static void registry_evicts_the_stalest_not_the_freshest() {
    RuuviRegistry r;
    for(uint8_t i=1;i<=8;++i) CHECK(r.observe(sensor(i,-60),static_cast<uint64_t>(i)*1000));
    CHECK(r.used_count()==8);
    CHECK(r.observe(sensor(0x20,-70),20000));            // 9th sensor replaces MAC ..01 (oldest)
    RuuviSeen out[8];
    const size_t n=r.snapshot(21000,out,8);
    CHECK(n==8);
    bool has01=false,has20=false,has08=false;
    for(size_t i=0;i<n;++i) {
        has01|=out[i].latest.mac[5]==0x01;has20|=out[i].latest.mac[5]==0x20;has08|=out[i].latest.mac[5]==0x08;
    }
    CHECK(!has01&&has20&&has08);
}

static void registry_rejects_implausible_values() {
    RuuviRegistry r;
    CHECK(!r.observe(sensor(0x01,-60,120.0),1000));       // 120 C
    CHECK(!r.observe(sensor(0x01,-60,-60.0),1000));
    CHECK(!r.observe(sensor(0x01,-60,24.0,101.0),1000));
    RuuviV5 nan_rh=sensor(0x01,-60);
    nan_rh.relative_humidity_pct=std::nan("");
    CHECK(!r.observe(nan_rh,1000));
    CHECK(r.used_count()==0);                              // nothing implausible is even listed
    CHECK(r.observe(sensor(0x01,-60,-30.0,5.0),1000));      // a very cold, dry room is fine
}

static std::array<uint8_t,6> mac(uint8_t last) {return {0xCB,0xB8,0x33,0x4C,0x88,last};}

static void names_are_validated_labels() {
    SensorNames n;
    CHECK(n.set(mac(1),"Olohuone")==NameResult::Ok);
    CHECK(!std::strcmp(n.get(mac(1)),"Olohuone"));
    CHECK(n.set(mac(1),"Makuuhuone")==NameResult::Ok&&!std::strcmp(n.get(mac(1)),"Makuuhuone"));   // rename
    CHECK(n.set(mac(2),"Ulko \xC3\xA4\xC3\xA4")==NameResult::Ok);                                    // UTF-8 allowed
    CHECK(n.set(mac(3),"<b>x</b>")==NameResult::BadName);
    CHECK(n.set(mac(3),"a\"b")==NameResult::BadName);
    CHECK(n.set(mac(3),"it's")==NameResult::BadName);
    CHECK(n.set(mac(3),"tab\there")==NameResult::BadName);
    CHECK(n.set(mac(3),"bad\xC3")==NameResult::BadName);                // truncated UTF-8
    CHECK(n.set(mac(3),"   ")==NameResult::BadName);
    CHECK(n.set(mac(3),"012345678901234567890123")==NameResult::BadName);    // 24 bytes: no room for the terminator
    CHECK(n.set(mac(3),"01234567890123456789012")==NameResult::Ok);          // 23 bytes is the maximum
    CHECK(n.set(mac(3),nullptr)==NameResult::BadName);
    std::array<uint8_t,6> zero{};
    CHECK(n.set(zero,"x")==NameResult::BadMac);
    CHECK(n.get(mac(9))==nullptr);
    CHECK(n.set(mac(1),"")==NameResult::Cleared&&n.get(mac(1))==nullptr);     // empty clears
}

static void names_table_is_bounded_and_load_is_strict() {
    SensorNames n;
    for(uint8_t i=1;i<=8;++i) CHECK(n.set(mac(i),"Anturi")==NameResult::Ok||i>=8);
    // table full: a ninth name is refused, an existing one can still be renamed
    SensorNames full;
    for(uint8_t i=1;i<=8;++i) CHECK(full.set(mac(i),"A")==NameResult::Ok);
    CHECK(full.set(mac(9),"B")==NameResult::TableFull);
    CHECK(full.set(mac(4),"C")==NameResult::Ok);
    // load() accepts a valid persisted table and refuses injected text
    SensorNames src;
    CHECK(src.set(mac(1),"Olohuone")==NameResult::Ok);
    SensorNames restored;
    CHECK(restored.load(src.entries())&&!std::strcmp(restored.get(mac(1)),"Olohuone"));
    auto bad=src.entries();
    std::strcpy(bad[0].name,"<script>");
    SensorNames victim;
    CHECK(victim.set(mac(7),"Keep")==NameResult::Ok);
    CHECK(!victim.load(bad));
    CHECK(!std::strcmp(victim.get(mac(7)),"Keep")&&victim.get(mac(1))==nullptr);   // failed load changes nothing
    auto unterminated=src.entries();
    std::memset(unterminated[0].name,'a',sizeof(unterminated[0].name));
    CHECK(!victim.load(unterminated));
}

int main() {
    registry_lists_and_ages();
    registry_evicts_the_stalest_not_the_freshest();
    registry_rejects_implausible_values();
    names_are_validated_labels();
    names_table_is_bounded_and_load_is_strict();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("ruuvi_registry_tests OK");
    return 0;
}
