#include "cottage/nvs_cache_reclaim_policy.hpp"
#include <cassert>
#include <string>
#include <vector>
using namespace enevina::cottage;
int main(){
    int attempts=0,erases=0;
    assert(persist_with_optional_cache_reclaim(
       [&]{++attempts;return true;},
       [&](const char*,const char*){++erases;return true;}));
    assert(attempts==1 && erases==0);
    attempts=0;erases=0;
    std::vector<std::string> deleted;
    assert(persist_with_optional_cache_reclaim(
       [&]{return ++attempts==2;},
       [&](const char* ns,const char* key){
            ++erases;
            deleted.emplace_back(std::string(ns)+"/"+key);
            return true;
       }));
    assert(attempts==2 && erases==1);
    assert(deleted[0]=="enevina_weather/weather_v2");
    attempts=0;erases=0;deleted.clear();
    assert(persist_with_optional_cache_reclaim(
       [&]{return ++attempts==3;},
       [&](const char* ns,const char* key){
           ++erases;deleted.emplace_back(std::string(ns)+"/"+key);
           return true;
       }));
    assert(attempts==4 && erases==3);
    assert(deleted[1]=="enevina_market/price_v2");
    attempts=0;erases=0;deleted.clear();
    assert(persist_with_optional_cache_reclaim(
       [&]{return ++attempts==4;},
       [&](const char* ns,const char* key){
           ++erases;deleted.emplace_back(std::string(ns)+"/"+key);
           return true;
       }));
    assert(attempts==4 && erases==3);
    assert(deleted[2]=="enevina_market/price_reg_v2");
    attempts=0;erases=0;
    assert(!persist_with_optional_cache_reclaim(
       [&]{++attempts;return false;},
       [&](const char*,const char*){++erases;return true;}));
    assert(attempts==4 && erases==3);
    attempts=0;erases=0;
    assert(!persist_with_optional_cache_reclaim(
       [&]{++attempts;return false;},
       [&](const char*,const char*){++erases;return false;}));
    assert(attempts==1 && erases==2);
}
