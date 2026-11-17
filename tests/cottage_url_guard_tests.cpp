#include "cottage/url_guard.hpp"
#include <cassert>
#include <initializer_list>
#include <cstdio>
using namespace enevina::cottage;
int main(){
    assert(valid_cn105_events_url("http://example-cn105.local/events"));
    assert(valid_cn105_events_url("http://cottage-cn105.local:8080/events"));
    assert(valid_shelly_base_url("http://example-shelly.local"));
    assert(valid_shelly_base_url("http://shelly.local:80"));
    for(const char* url:{
        "http://127.0.0.1/rpc/Switch.Set?id=0&on=false",
        "http://example-shelly.local/events?mode=off",
        "http://example-shelly.local/command",
        "http://user:password@host/events",
        "http://host//events",
        "https://host/events",
        "http://host/events#x",
        "http://host/%65vents",
        "http://host:0/events",
        "http://host:99999/events",
        "http://host/../events",
        ""
    }) assert(!valid_cn105_events_url(url));
    for(const char* url:{
        "http://shelly.local/rpc/Switch.Set?id=0&on=false",
        "http://shelly.local/rpc/Switch.GetStatus?id=0",
        "http://shelly.local/",
        "http://shelly.local?x=1",
        "http://user:password@shelly.local",
        "http://shelly..local",
        "http://shelly.local#x",
        "https://shelly.local",
        ""
    })assert(!valid_shelly_base_url(url));
    std::puts("PASS hard-allowlist CN105/Shelly HTTP URL safety tests");
}
