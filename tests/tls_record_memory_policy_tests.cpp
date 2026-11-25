#include "cottage/tls_record_memory_policy.hpp"
#include <cassert>
#include <limits>
using enevina::cottage::tls_record_use_psram;
int main(){
    constexpr size_t in=16717,out=16717;
    assert(tls_record_use_psram(true,1,in,in,out));
    assert(tls_record_use_psram(true,in,1,in,out));
    assert(!tls_record_use_psram(false,1,in,in,out));
    assert(!tls_record_use_psram(true,1,4096,in,out));
    assert(!tls_record_use_psram(true,1,in+1,in,out));
    assert(!tls_record_use_psram(true,0,in,in,out));
    assert(!tls_record_use_psram(true,std::numeric_limits<size_t>::max(),2,in,out));
    assert(tls_record_use_psram(true,1,17000,16717,17000));
    assert(!tls_record_use_psram(true,1,8192,8192,8192));
}
