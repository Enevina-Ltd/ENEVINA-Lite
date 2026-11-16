#include "pump/serial_freshness.hpp"
#include <cstdio>
#include <cstdlib>
using namespace enevina;
static void check(bool condition,const char* message) {
    if(!condition) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
int main() {
    check(!confirmed_pump_serial_update({true,false,0,0}),
        "HTTP 200 with unknown serial counter never proves freshness");
    check(!confirmed_pump_serial_update({true,true,42,42}),
        "cached HTTP values with stagnant serial count must be stale");
    check(!confirmed_pump_serial_update({false,true,42,43}),
        "missing current HTTP sample must fail closed");
    check(confirmed_pump_serial_update({true,true,42,43}),
        "advancing counter permits freshness if a trusted source is added");
    check(!confirmed_pump_serial_update({true,true,UINT64_MAX,0}),
        "counter reset/wrap must fail closed until reestablished");
    std::puts("PASS pump serial evidence vs HeishaMon cached HTTP data");
}
