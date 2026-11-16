#include <cassert>
#include "cottage/memory_policy.hpp"
using enevina::cottage::internal_memory_admissible;
int main() {
    constexpr size_t kib=1024;
    static_assert(internal_memory_admissible(78*kib,16*kib,78*kib,16*kib));
    static_assert(!internal_memory_admissible(78*kib-1,16*kib,78*kib,16*kib));
    static_assert(!internal_memory_admissible(78*kib,16*kib-1,78*kib,16*kib));
    assert(internal_memory_admissible(110*kib,32*kib,96*kib,24*kib));
    assert(!internal_memory_admissible(94*kib,32*kib,96*kib,24*kib));
    assert(!internal_memory_admissible(110*kib,8*kib,78*kib,16*kib));
    assert(!internal_memory_admissible(0,0,1,1));
}
