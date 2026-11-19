// Regional price foundation: zone table, EU civil time, Nord Pool market day
// and the strict day-ahead parser, exercised against REAL recorded responses
// from the Nord Pool Data Portal (tests/data/nordpool, fetched 2026-11-19).
#include "forecast/eu_time.hpp"
#include "forecast/market_zones.hpp"
#include "forecast/nordpool_day_ahead.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifndef ENEVINA_TEST_DATA_DIR
#error "ENEVINA_TEST_DATA_DIR must point at tests/data"
#endif

static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)

static std::string read_file(const std::string& name) {
    // C stdio: the Windows MinGW toolchain on CI/dev PCs can pick up a
    // mismatched libstdc++ DLL whose iostreams crash.
    const std::string path=std::string(ENEVINA_TEST_DATA_DIR)+"/nordpool/"+name;
    std::FILE* f=std::fopen(path.c_str(),"rb");
    std::string out;
    if(!f) return out;
    char buf[4096];
    size_t n;
    while((n=std::fread(buf,1,sizeof(buf),f))>0) out.append(buf,n);
    std::fclose(f);
    return out;
}
static int64_t utc(int y,int m,int d,int h,int mi=0) {
    return enevina::days_from_civil_date(y,static_cast<unsigned>(m),
        static_cast<unsigned>(d))*86400+h*3600+mi*60;
}

static void zone_table() {
    using namespace enevina;
    CHECK(market_country_count>=14);
    std::set<std::string> ids;
    for(const auto& c:market_countries) {
        CHECK(std::strlen(c.iso)==2);
        CHECK(std::strlen(c.currency)==3);
        CHECK(c.std_offset_hours>=0 && c.std_offset_hours<=3);
        CHECK(c.zone_count>=1);
        for(uint8_t i=0;i<c.zone_count;++i) {
            CHECK(ids.insert(c.zones[i].id).second);          // unique ids
            CHECK(np_detail::upper_alnum_dash(c.zones[i].np_area,2,6));
            CHECK(valid_market_region(c.iso,c.zones[i].id));
        }
    }
    CHECK(!valid_market_region("SE","NO1"));      // zone must belong to country
    CHECK(!valid_market_region("ES","ES"));        // no verified data -> unsupported
    CHECK(!valid_market_region("GB","GB"));
    CHECK(find_market_country("SE")->zone_count==4);
    CHECK(find_market_country("NO")->zone_count==5);
    CHECK(find_market_country("DK")->zone_count==2);
    CHECK(implied_market_zone(*find_market_country("FI"))!=nullptr);
    CHECK(implied_market_zone(*find_market_country("SE"))==nullptr);  // must choose
    CHECK(!std::strcmp(find_market_country("SE")->currency,"SEK"));
    CHECK(!std::strcmp(find_market_country("PL")->currency,"PLN"));
    CHECK(!std::strcmp(find_market_country("EE")->currency,"EUR"));
}

static void eu_time() {
    using namespace enevina;
    std::tm t{};
    // Finland (+2): winter 2026-11-18 12:00Z -> 14:00; summer 2026-07-01 12:00Z -> 15:00.
    CHECK(eu_local_time(utc(2026,11,18,12),2,t) && t.tm_hour==14);
    CHECK(eu_local_time(utc(2026,7,1,12),2,t) && t.tm_hour==15);
    CHECK(eu_local_time(utc(2026,7,1,12),1,t) && t.tm_hour==14);   // Sweden/Germany
    CHECK(eu_local_time(utc(2026,7,1,12),0,t) && t.tm_hour==13);   // UK/Ireland/Portugal
    // 2026 transitions: last Sunday of March = 29th, of October = 25th, 01:00 UTC.
    bool s=false;
    CHECK(eu_summer_time(utc(2026,3,29,0,59),s) && !s);
    CHECK(eu_summer_time(utc(2026,3,29,1,0),s) && s);
    CHECK(eu_summer_time(utc(2026,10,25,0,59),s) && s);
    CHECK(eu_summer_time(utc(2026,10,25,1,0),s) && !s);
    CHECK(!eu_local_time(0,9,t));            // unsupported offset rejected
    // Market date is CET/CEST, NOT Finnish local date.
    char d[16];
    // Finnish local 2026-11-19 00:30 = 2026-11-18 22:30Z = 23:30 CET 2026-11-18.
    CHECK(nordpool_market_date(utc(2026,11,18,22,30),0,d,sizeof(d)) && !std::strcmp(d,"2026-11-18"));
    CHECK(nordpool_market_date(utc(2026,11,18,22,30),1,d,sizeof(d)) && !std::strcmp(d,"2026-11-19"));
    CHECK(nordpool_market_date(utc(2026,11,18,23,0),0,d,sizeof(d)) && !std::strcmp(d,"2026-11-19"));
    // Calendar-day arithmetic across month/year ends and DST changes.
    CHECK(nordpool_market_date(utc(2026,12,31,12),1,d,sizeof(d)) && !std::strcmp(d,"2027-01-01"));
    CHECK(nordpool_market_date(utc(2028,2,28,12),1,d,sizeof(d)) && !std::strcmp(d,"2028-02-29"));
    CHECK(nordpool_market_date(utc(2026,3,28,22,30),1,d,sizeof(d)) && !std::strcmp(d,"2026-03-29"));
    CHECK(nordpool_market_date(utc(2026,3,28,22,30),2,d,sizeof(d)) && !std::strcmp(d,"2026-03-30"));
    CHECK(nordpool_market_date(utc(2026,10,24,21,59),1,d,sizeof(d)) && !std::strcmp(d,"2026-10-25"));
    CHECK(!nordpool_market_date(0,7,d,sizeof(d)));
    CHECK(!nordpool_market_date(0,0,d,5));
}

static void url_building() {
    using namespace enevina;
    char u[220];
    CHECK(build_day_ahead_url("SE3","SEK","2026-11-18",u,sizeof(u)));
    CHECK(std::string(u).find("currency=SEK&market=DayAhead&deliveryArea=SE3&date=2026-11-18")!=std::string::npos);
    CHECK(!build_day_ahead_url("SE3&x=1","SEK","2026-11-18",u,sizeof(u)));
    CHECK(!build_day_ahead_url("se3","SEK","2026-11-18",u,sizeof(u)));
    CHECK(!build_day_ahead_url("SE3","SE","2026-11-18",u,sizeof(u)));
    CHECK(!build_day_ahead_url("SE3","SEK","2026-11-18\n",u,sizeof(u)));
    CHECK(!build_day_ahead_url("SE3","SEK","2026-11-18",u,20));
    CHECK(!build_day_ahead_url(nullptr,"SEK","2026-11-18",u,sizeof(u)));
}

static std::vector<enevina::MarketPrice> parse_ok(const std::string& file,
    const char* area,const char* currency,size_t expect) {
    std::vector<enevina::MarketPrice> out(120);
    size_t n=0;
    const std::string body=read_file(file);
    CHECK(!body.empty());
    const auto e=enevina::parse_day_ahead(body.data(),body.size(),area,currency,out.data(),out.size(),n);
    if(e!=enevina::DayAheadError::Ok||n!=expect)
        std::fprintf(stderr,"parse %s -> %s n=%zu\n",file.c_str(),enevina::day_ahead_error_name(e),n);
    CHECK(e==enevina::DayAheadError::Ok);
    CHECK(n==expect);
    out.resize(n);
    return out;
}

static void real_fixtures_all_zones() {
    using namespace enevina;
    int parsed=0;
    for(const auto& c:market_countries) {
        for(uint8_t i=0;i<c.zone_count;++i) {
            const auto& z=c.zones[i];
            const std::string f=std::string(z.np_area)+"_2026-11-18.json";
            auto v=parse_ok(f,z.np_area,"EUR",96);   // fixtures are EUR series
            if(v.size()==96) {
                ++parsed;
                CHECK(v.front().start_utc==utc(2026,11,17,23));   // CET day start
                CHECK(v.back().end_utc==utc(2026,11,18,23));
                for(size_t k=1;k<v.size();++k) CHECK(v[k].start_utc==v[k-1].end_utc);
            }
        }
    }
    CHECK(parsed==22);   // every listed bidding zone has a real, complete day
}

static void local_currency_series() {
    auto se=parse_ok("SE3_SEK_2026-11-18.json","SE3","SEK",96);
    auto eur=parse_ok("SE3_2026-11-18.json","SE3","EUR",96);
    // SEK/EUR ~ 11.1 on the day: same market, same shape, different unit.
    if(se.size()==96&&eur.size()==96) {
        const double ratio=se[40].eur_per_mwh/eur[40].eur_per_mwh;
        CHECK(ratio>10.5&&ratio<11.8);
    }
    parse_ok("PL_PLN_2026-11-18.json","PL","PLN",96);
}

static void dst_market_days() {
    auto fi=parse_ok("FI_2026-10-25.json","FI","EUR",100);   // 25 h autumn day
    if(fi.size()==100) {
        CHECK(fi.front().start_utc==utc(2026,10,24,22));
        CHECK(fi.back().end_utc==utc(2026,10,25,23));
    }
    parse_ok("SE3_2026-10-25.json","SE3","EUR",100);
    parse_ok("NL_2026-10-25.json","NL","EUR",100);
}

static void rejects_wrong_or_damaged_data() {
    using namespace enevina;
    std::vector<MarketPrice> out(120);
    size_t n=0;
    const std::string fi=read_file("FI_2026-11-18.json");
    // Wrong area requested -> never reads another area's number.
    CHECK(parse_day_ahead(fi.data(),fi.size(),"SE3","EUR",out.data(),out.size(),n)==DayAheadError::WrongArea);
    CHECK(n==0);
    // Wrong currency never silently accepted.
    CHECK(parse_day_ahead(fi.data(),fi.size(),"FI","SEK",out.data(),out.size(),n)==DayAheadError::WrongCurrency);
    // Truncated body.
    CHECK(parse_day_ahead(fi.data(),fi.size()/2,"FI","EUR",out.data(),out.size(),n)==DayAheadError::BadJson);
    CHECK(n==0);
    // A missing slot (gap) is rejected, not filled.
    std::string gap=fi;
    const size_t a=gap.find("{\"deliveryStart\":\"2026-11-18T00:00:00Z\"");
    const size_t b=gap.find("{\"deliveryStart\":\"2026-11-18T00:15:00Z\"");
    CHECK(a!=std::string::npos&&b!=std::string::npos);
    if(a!=std::string::npos&&b!=std::string::npos) {
        gap.erase(a,b-a);
        const auto e=parse_day_ahead(gap.data(),gap.size(),"FI","EUR",out.data(),out.size(),n);
        CHECK(e==DayAheadError::BadSlots);
        CHECK(n==0);
    }
    // A wrong-length slot.
    std::string longslot=fi;
    const size_t p=longslot.find("2026-11-17T23:15:00Z\",\"deliveryEnd\":\"2026-11-17T23:30:00Z");
    if(p!=std::string::npos) {
        longslot.replace(p,46,"2026-11-17T23:15:00Z\",\"deliveryEnd\":\"2026-11-17T23:45:00Z");
        CHECK(parse_day_ahead(longslot.data(),longslot.size(),"FI","EUR",out.data(),out.size(),n)!=DayAheadError::Ok);
    }
    // Non-numeric / absurd price values (unit mix-up) are rejected.
    std::string bad=fi;
    const size_t q=bad.find("\"FI\":39.90");
    CHECK(q!=std::string::npos);
    if(q!=std::string::npos) {
        std::string x=bad;x.replace(q,10,"\"FI\":9e99");
        CHECK(parse_day_ahead(x.data(),x.size(),"FI","EUR",out.data(),out.size(),n)==DayAheadError::BadValue);
        x=bad;x.replace(q,10,"\"FI\":null");
        CHECK(parse_day_ahead(x.data(),x.size(),"FI","EUR",out.data(),out.size(),n)==DayAheadError::BadValue);
        x=bad;x.replace(q,10,"\"FI\":-9.9");   // negative spot is legitimate
        CHECK(parse_day_ahead(x.data(),x.size(),"FI","EUR",out.data(),out.size(),n)==DayAheadError::Ok);
        CHECK(out[0].eur_per_mwh==-9.9);
    }
    // Capacity too small is an error, not a silent truncation.
    std::vector<MarketPrice> small(50);
    CHECK(parse_day_ahead(fi.data(),fi.size(),"FI","EUR",small.data(),small.size(),n)==DayAheadError::Capacity);
    CHECK(n==0);
    // Garbage / null inputs.
    CHECK(parse_day_ahead(nullptr,0,"FI","EUR",out.data(),out.size(),n)==DayAheadError::BadRequest);
    CHECK(parse_day_ahead("{}",2,"FI","EUR",out.data(),out.size(),n)==DayAheadError::BadJson);
    CHECK(parse_day_ahead(fi.data(),fi.size(),"fi","EUR",out.data(),out.size(),n)==DayAheadError::BadRequest);
}

int main() {
    zone_table();
    eu_time();
    url_building();
    real_fixtures_all_zones();
    local_currency_series();
    dst_market_days();
    rejects_wrong_or_damaged_data();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("market_zones_tests OK");
    return 0;
}
