#include "cottage/cn105_read_retry.hpp"
#include <cassert>
#include <initializer_list>
using namespace enevina::cottage;

int main() {
    assert(cn105_read_max_attempts==2);
    assert(cn105_read_response_timeout_ms>=4000);
    assert(cn105_read_connect_timeout_ms>=2000);
    assert(cn105_should_retry_read(-11,0));
    assert(cn105_should_retry_read(-5,0));
    assert(!cn105_should_retry_read(-11,1));
    assert(!cn105_should_retry_read(-5,1));
    for(int code : {200,201,204,301,400,401,403,404,500,0,-1,-2,-3,-4,-6,-8,-10}) {
        assert(!cn105_should_retry_read(code,0));
    }
}
