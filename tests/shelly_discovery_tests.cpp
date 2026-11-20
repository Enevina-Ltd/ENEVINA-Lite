// Shelly discovery classification. Documents follow Shelly's published API
// format; they are NOT recordings from a device (see cottage/shelly_discovery.hpp).
#include "cottage/shelly_discovery.hpp"
#include <cstdio>
#include <cstring>
#include <string>

using namespace enevina::cottage;
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)

static bool info(const std::string& s,ShellyInfo& out) {return parse_shelly_info(s.data(),s.size(),out);}

static void gen2_documents() {
    ShellyInfo i;
    CHECK(info(R"({"name":null,"id":"shellyplus1pm-a8032abe7d94","mac":"A8032ABE7D94","slot":0,"model":"SNSW-001P16EU","gen":2,"fw_id":"20230913-114008/1.0.3-g7e5e3b9","ver":"1.0.3","app":"Plus1PM","auth_en":false,"auth_domain":null})",i));
    CHECK(i.generation==2&&!std::strcmp(i.id,"shellyplus1pm-a8032abe7d94")&&!std::strcmp(i.app,"Plus1PM"));
    CHECK(i.support==ShellySupport::Supported&&!i.auth);
    // Gen3 uses the same fields.
    CHECK(info(R"({"id":"shelly1pmmini-aabbccddeeff","model":"S3SW-001P8EU","gen":3,"app":"Mini1PMG3","auth_en":false})",i));
    CHECK(i.generation==3&&i.support==ShellySupport::Supported);
    // Password protected: listed, explained, never accepted.
    CHECK(info(R"({"id":"shellyplus1pm-a8032abe7d94","gen":2,"auth_en":true,"auth_domain":"shellyplus1pm-a8032abe7d94"})",i));
    CHECK(i.support==ShellySupport::AuthRequired&&i.auth);
}

static void gen1_documents() {
    ShellyInfo i;
    CHECK(info(R"({"type":"SHSW-PM","mac":"A4CF12F4D2B1","auth":false,"fw":"20230913-112003/v1.14.0-gcb84623","discoverable":false,"num_outputs":1,"num_meters":1})",i));
    CHECK(i.generation==1&&i.support==ShellySupport::Gen1NotSupported);    // reader needs the Gen2 RPC
}

static void rejects_things_that_are_not_shelly() {
    ShellyInfo i;
    CHECK(!info("{}",i));
    CHECK(!info(R"({"id":"router","gen":2,"auth_en":false})",i));            // id must start with shelly
    CHECK(!info(R"({"id":"shellyx","gen":1,"auth_en":false})",i));            // gen 1 is not a Gen2 document
    CHECK(!info(R"({"id":"shellyplus1pm-aa","gen":2})",i));                    // auth state unknown -> refuse
    CHECK(!info(R"({"type":"Apache","mac":"A4CF12F4D2B1"})",i));              // not a Shelly type
    CHECK(!info(R"({"id":"shelly<b>","gen":2,"auth_en":false})",i));          // markup in the id
    CHECK(!info(R"(<html>Welcome</html>)",i));
    CHECK(!info(R"({"id":"shellyplus1pm-a8032abe7d94","gen":2,"auth_en":false)",i));   // truncated
    CHECK(!info(R"({"id":"shellyplus1pm-aa","gen":"2","auth_en":false})",i));          // gen must be a number
    CHECK(!parse_shelly_info(nullptr,0,i));
    CHECK(!info(std::string(5000,' '),i));
    CHECK(i.support==ShellySupport::NotShelly);                                // failed parse resets the output
}

static void power_reading() {
    double w=-1;
    const std::string ok=R"({"id":0,"source":"init","output":true,"apower":412.7,"voltage":231.2,"current":1.9,"aenergy":{"total":18234.5,"by_minute":[0,0,0],"minute_ts":1795000000},"temperature":{"tC":38.1,"tF":100.6}})";
    CHECK(parse_shelly_power(ok.data(),ok.size(),w)&&std::fabs(w-412.7)<1e-9);
    const std::string zero=R"({"id":0,"output":false,"apower":0})";
    CHECK(parse_shelly_power(zero.data(),zero.size(),w)&&w==0);
    for(const char* bad:{R"({"id":0})",R"({"apower":"412"})",R"({"apower":null})",R"({"apower":1e9})",
                         R"({"apower":-5000})",R"({"apower":412.7)","[]","nope"}) {
        w=-7;
        CHECK(!parse_shelly_power(bad,std::strlen(bad),w));
        CHECK(w==-7);                                                           // failed parse leaves the value alone
    }
}

static void host_and_url_rules() {
    CHECK(shelly_hostname_candidate("shellyplus1pm-a8032abe7d94"));
    CHECK(shelly_hostname_candidate("Shelly1PM-A4CF12F4D2B1"));
    CHECK(!shelly_hostname_candidate("printer-shelly"));
    CHECK(!shelly_hostname_candidate("shelly"));                // too short to be a device
    CHECK(!shelly_hostname_candidate("shelly 1pm"));
    CHECK(!shelly_hostname_candidate("shelly/../x"));
    CHECK(!shelly_hostname_candidate(nullptr));
    char url[32];
    const uint8_t lan[4]={192,168,1,30},ten[4]={10,0,0,5},pub[4]={8,8,8,8},lo[4]={127,0,0,1},bc[4]={192,168,1,255};
    CHECK(shelly_lan_base_url(lan,url,sizeof(url))&&!std::strcmp(url,"http://192.168.1.30"));
    CHECK(shelly_lan_base_url(ten,url,sizeof(url)));
    CHECK(!shelly_lan_base_url(pub,url,sizeof(url)));             // never a public address
    CHECK(!shelly_lan_base_url(lo,url,sizeof(url)));
    CHECK(!shelly_lan_base_url(bc,url,sizeof(url)));
    CHECK(!shelly_lan_base_url(lan,url,10));                      // buffer too small
}

static void full_length_rpc_url_is_not_truncated() {
    char path[64]{};
    const uint8_t ip[4]={192,168,255,254};
    char base[32]{};
    CHECK(shelly_lan_base_url(ip,base,sizeof(base)));
    CHECK(shelly_read_url(base,"/shelly",path,sizeof(path)));
    CHECK(!std::strcmp(path,"http://192.168.255.254/shelly"));
    CHECK(shelly_read_url(base,"/rpc/Switch.GetStatus?id=0",path,sizeof(path)));
    CHECK(!std::strcmp(path,"http://192.168.255.254/rpc/Switch.GetStatus?id=0"));
    char too_short[48]{};
    CHECK(!shelly_read_url(base,"/rpc/Switch.GetStatus?id=0",too_short,sizeof(too_short)));
    CHECK(!shelly_read_url(nullptr,"/shelly",path,sizeof(path)));
    CHECK(!shelly_read_url(base,"/shelly",nullptr,sizeof(path)));
}
static void scope_gates_the_pump_model() {
    ShellyScope s{};
    CHECK(parse_shelly_scope("heat_pump",s)&&s==ShellyScope::HeatPump);
    CHECK(parse_shelly_scope("whole_house",s)&&s==ShellyScope::WholeHouse);
    CHECK(!parse_shelly_scope("pump",s)&&!parse_shelly_scope(nullptr,s));
    CHECK(shelly_may_feed_pump_model(ShellyScope::HeatPump));
    CHECK(!shelly_may_feed_pump_model(ShellyScope::Unknown));      // default: not proven
    CHECK(!shelly_may_feed_pump_model(ShellyScope::WholeHouse));
    CHECK(!std::strcmp(shelly_scope_name(ShellyScope::HeatPump),"heat_pump"));
}

int main() {
    gen2_documents();
    gen1_documents();
    rejects_things_that_are_not_shelly();
    power_reading();
    host_and_url_rules();
    full_length_rpc_url_is_not_truncated();
    scope_gates_the_pump_model();
    if(failures) {std::fprintf(stderr,"%d failures\n",failures);return 1;}
    std::puts("shelly_discovery_tests OK");
    return 0;
}
