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
    // Field evidence 2026-11-18: free internal RAM 79,196 B, largest
    // contiguous block 42,996-59,380 B. The old 80-KiB admission check
    // denied >230 scans, leaving RuuviTag stale for >43 minutes.
    static_assert(ble_scan_min_internal_bytes==64*kib);
    static_assert(ble_scan_min_contiguous_bytes==20*kib);
    static_assert(internal_memory_admissible(79196,42996,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
    static_assert(internal_memory_admissible(90*kib,28*kib,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
    static_assert(!internal_memory_admissible(64*kib-1,43*kib,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
    static_assert(!internal_memory_admissible(79*kib,19*kib,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
    static_assert(!internal_memory_admissible(39*kib,10*kib,
        ble_scan_min_internal_bytes,ble_scan_min_contiguous_bytes));
}
