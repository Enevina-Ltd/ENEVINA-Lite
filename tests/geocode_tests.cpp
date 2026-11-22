// City search: encoding, URL, parser and filters against REAL Photon
// (OpenStreetMap) responses recorded 2026-11-19 in tests/data/geocode.
#include "forecast/geocode.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef ENEVINA_TEST_DATA_DIR
#error "ENEVINA_TEST_DATA_DIR must point at tests/data"
#endif
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)

static std::string read_file(const char* name) {
    const std::string path=std::string(ENEVINA_TEST_DATA_DIR)+"/geocode/"+name;
    std::FILE* f=std::fopen(path.c_str(),"rb");
    std::string out;
    if(!f) return out;
    char buf[4096];
    size_t n;
    while((n=std::fread(buf,1,sizeof(buf),f))>0) out.append(buf,n);
    std::fclose(f);
    return out;
}
using enevina::GeoPlace;

static void encoding() {
    char b[160];
    CHECK(enevina::geocode_encode_query("Kang",b,sizeof(b))&&!std::strcmp(b,"Kang"));
    CHECK(enevina::geocode_encode_query("Malm\xC3\xB6",b,sizeof(b))&&!std::strcmp(b,"Malm%C3%B6"));
    CHECK(enevina::geocode_encode_query("New York",b,sizeof(b))&&!std::strcmp(b,"New%20York"));
    CHECK(enevina::geocode_encode_query("a&b=c#d",b,sizeof(b))&&
          !std::strcmp(b,"a%26b%3Dc%23d"));                   // nothing leaks into the URL
    CHECK(!enevina::geocode_encode_query("Ka",b,sizeof(b)));  // too short
    CHECK(!enevina::geocode_encode_query("",b,sizeof(b)));
    CHECK(!enevina::geocode_encode_query(nullptr,b,sizeof(b)));
    CHECK(!enevina::geocode_encode_query("bad\nline",b,sizeof(b)));
    CHECK(!enevina::geocode_encode_query(std::string(49,'a').c_str(),b,sizeof(b)));
    CHECK(!enevina::geocode_encode_query("Kangasala",b,5));   // output too small
    // Two-byte characters count as one character each, not two.
    CHECK(!enevina::geocode_encode_query("\xC3\xA4\xC3\xB6",b,sizeof(b)));
    char url[300];
    CHECK(enevina::build_geocode_url("Kang","fi",url,sizeof(url)));
    CHECK(std::string(url).find("q=Kang&limit=20&layer=city")!=std::string::npos);
    CHECK(std::string(url).find("&lang=")==std::string::npos);
    CHECK(enevina::build_geocode_url("Kang","xx",url,sizeof(url)));      // falls back to en
    CHECK(std::string(url).find("lang=en")!=std::string::npos);
    CHECK(!enevina::build_geocode_url("Kang","en",url,20));
}

static std::vector<GeoPlace> parse(const char* file,const char* country,bool expect_ok=true) {
    std::vector<GeoPlace> v(8);
    size_t n=0;
    const std::string body=read_file(file);
    CHECK(!body.empty());
    const bool ok=enevina::parse_geocode(body.data(),body.size(),country,v.data(),v.size(),n);
    CHECK(ok==expect_ok);
    v.resize(n);
    return v;
}

static void real_responses() {
    // The spec's example: typing "Kang" must offer Kangasala, Finland.
    auto fi=parse("photon_kang.json","FI");
    CHECK(fi.size()>=2);
    bool kangasala=false;
    for(const auto& p:fi) {
        CHECK(!std::strcmp(p.country_iso,"FI"));
        if(!std::strcmp(p.name,"Kangasala")) {
            kangasala=true;
            CHECK(std::fabs(p.lat-61.4715)<0.01&&std::fabs(p.lon-24.3262)<0.01);
            CHECK(!std::strcmp(p.region,"Pirkanmaa"));
            CHECK(!std::strcmp(p.country_name,"Finland"));
            char t[32];
            CHECK(enevina::geocode_latlon_text(p,t,sizeof(t))&&!std::strcmp(t,"61.4715,24.3262"));
        }
    }
    CHECK(kangasala);
    // Without a country filter other countries' places appear too.
    auto all=parse("photon_kang.json",nullptr);
    CHECK(!all.empty()&&all.size()<=8);       // capped at the caller's capacity
    bool other=false;
    for(const auto& p:all) other|=std::strcmp(p.country_iso,"FI")!=0;
    CHECK(other);
    // Farmyards / isolated dwellings called Malmo are not cities.
    auto se=parse("photon_malmo.json","SE");
    CHECK(!se.empty());
    CHECK(!std::strcmp(se[0].name,"Malm\xC3\xB6")&&std::fabs(se[0].lat-55.6)<0.2&&
          std::fabs(se[0].lon-13.0)<0.2);
    size_t same=0;
    for(const auto& p:se) if(!std::strcmp(p.name,"Malm\xC3\xB6")&&
        !std::strcmp(p.region,se[0].region)) ++same;
    CHECK(same==1);                           // duplicates collapsed
    auto ee=parse("photon_tallinn.json","EE");
    CHECK(!ee.empty()&&!std::strcmp(ee[0].name,"Tallinn")&&std::fabs(ee[0].lat-59.437)<0.1);
    auto hel=parse("photon_helsin.json","FI");
    CHECK(!hel.empty()&&!std::strcmp(hel[0].name,"Helsinki"));
    // Wrong-country filter yields nothing, not another country's city.
    auto none=parse("photon_kang.json","SE");
    CHECK(none.empty());
    // No match is a valid, empty answer.
    auto empty=parse("photon_zzqxj.json","FI");
    CHECK(empty.empty());
}

static std::string feature(const char* name,const char* cc,const char* coords) {
    return std::string("{\"type\":\"Feature\",\"properties\":{\"osm_key\":\"place\","
        "\"osm_value\":\"city\",\"name\":\"")+name+"\",\"countrycode\":\""+cc+
        "\"},\"geometry\":{\"type\":\"Point\",\"coordinates\":"+coords+"}}";
}
static std::string collection(const std::string& a,const std::string& b="") {
    return "{\"features\":["+a+(b.empty()?"":","+b)+"]}";
}

static void rejects_damaged_or_hostile() {
    std::vector<GeoPlace> v(8);
    size_t n=99;
    const std::string kang=read_file("photon_kang.json");
    CHECK(!enevina::parse_geocode(kang.data(),kang.size()/2,"FI",v.data(),v.size(),n)&&n==0);
    CHECK(!enevina::parse_geocode("{\"features\":42}",16,"FI",v.data(),v.size(),n)&&n==0);
    CHECK(!enevina::parse_geocode(nullptr,0,"FI",v.data(),v.size(),n)&&n==0);
    CHECK(!enevina::parse_geocode(kang.data(),kang.size(),"FI",v.data(),0,n));
    // A name with markup is skipped, never echoed to the dashboard.
    std::string s=collection(feature("<script>x</script>","FI","[24.0,61.0]"),
                             feature("Okaycity","FI","[24.0,61.0]"));
    CHECK(enevina::parse_geocode(s.data(),s.size(),"FI",v.data(),v.size(),n));
    CHECK(n==1&&!std::strcmp(v[0].name,"Okaycity"));
    // Out-of-range coordinates, bad country codes and wrong arity are dropped.
    s=collection(feature("Farcity","FI","[24.0,961.0]"));
    CHECK(enevina::parse_geocode(s.data(),s.size(),"FI",v.data(),v.size(),n)&&n==0);
    s=collection(feature("Longcc","fin","[24.0,61.0]"));
    CHECK(enevina::parse_geocode(s.data(),s.size(),nullptr,v.data(),v.size(),n)&&n==0);
    s=collection(feature("Lowercc","fi","[24.0,61.0]"));
    CHECK(enevina::parse_geocode(s.data(),s.size(),nullptr,v.data(),v.size(),n)&&n==0);
    s=collection(feature("Three","FI","[24.0,61.0,9]"));
    CHECK(enevina::parse_geocode(s.data(),s.size(),"FI",v.data(),v.size(),n)&&n==0);
    s=collection(feature("Strings","FI","[\"24.0\",\"61.0\"]"));
    CHECK(enevina::parse_geocode(s.data(),s.size(),"FI",v.data(),v.size(),n)&&n==0);
    s=collection(feature("Valid","FI","[24.0,61.0]"));
    CHECK(enevina::parse_geocode(s.data(),s.size(),"FI",v.data(),v.size(),n)&&n==1);
    CHECK(v[0].lon==24.0&&v[0].lat==61.0);          // [lon,lat] order
    char t[8];
    GeoPlace far{};far.lat=99;
    CHECK(!enevina::geocode_latlon_text(far,t,sizeof(t)));
}

int main() {
    encoding();
    real_responses();
    rejects_damaged_or_hostile();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("geocode_tests OK");
    return 0;
}
