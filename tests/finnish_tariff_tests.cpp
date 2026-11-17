#include "cottage/finnish_tariff.hpp"
#include <cstdio>
#include <cstdlib>
using namespace enevina::cottage;
static void check(bool v, const char* m) {
    if(!v) {std::fprintf(stderr,"FAIL %s\n",m);std::exit(1);}
}
static void hour(int64_t ts,int expected) {
    int actual=-1;
    check(finnish_local_hour(ts,actual),"UTC conversion");
    if(actual!=expected){std::fprintf(stderr,"Wrong hour %d expected %d\n",actual,expected);std::exit(1);}
}
static void day(int64_t ts,bool expected) {
    bool actual=!expected;
    check(finnish_day_transfer(ts,actual) && actual==expected,"day/night tariff");
}
int main(){
    hour(1774745940,2); // 2026-03-29 00:59 UTC, before spring transition
    hour(1774746000,4); // 01:00 UTC, spring jump skips 03:xx
    hour(1792889940,3); // 2026-10-25 00:59 UTC, summer time
    hour(1792890000,3); // 01:00 UTC, winter time repeats 03:xx
    hour(1794805140,6);day(1794805140,false);
    hour(1794805200,7);day(1794805200,true);
    hour(1794859140,21);day(1794859140,true);
    hour(1794859200,22);day(1794859200,false);
    hour(1782878340,6);day(1782878340,false);
    hour(1782878400,7);day(1782878400,true);
    std::puts("PASS deterministic Finnish day/night and DST boundaries");
}
