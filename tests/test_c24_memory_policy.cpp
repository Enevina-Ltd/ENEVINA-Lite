#include <cassert>
#include "cottage/memory_policy.hpp"
using enevina::cottage::internal_memory_admissible;
using enevina::cottage::ble_scan_min_internal_bytes;
using enevina::cottage::ble_scan_min_contiguous_bytes;
int main() {
    constexpr size_t kib=1024;
    static_assert(internal_memory_admissible(78*kib,16*kib,78*kib,16*kib));
    static_assert(!internal_memory_admissible(78*kib-1,16*kib,78*kib,16*kib));
    static_assert(!internal_memory_admissible(78*kib,16*kib-1,78*kib,16*kib));
    assert(internal_memory_admissible(110*kib,32*kib,96*kib,24*kib));
    assert(!internal_memory_admissible(94*kib,32*kib,96*kib,24*kib));
    assert(!internal_memory_admissible(110*kib,8*kib,78*kib,16*kib));
    assert(!internal_memory_admissible(0,0,1,1));
    // Regression: observed normal load was ~90 KiB internal free.
    // The old 96 KiB guard permanently starved BLE, leaving stale RuuviTag.
    static_assert(internal_memory_admissible(90*kib,28*kib,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
    static_assert(!internal_memory_admissible(78*kib,28*kib,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
    static_assert(!internal_memory_admissible(90*kib,19*kib,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
    static_assert(!internal_memory_admissible(39*kib,10*kib,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
}
