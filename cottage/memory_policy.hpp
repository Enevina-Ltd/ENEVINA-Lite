#pragma once
#include <cstddef>
namespace enevina::cottage {
// The ESP32-S3 test/field controller has ~79,200 bytes internal RAM free
// after Wi-Fi, CN105 SSE, Nord Pool and FMI are initialized. An 80-KiB BLE
// threshold permanently denies all scans even though the largest free block
// remains >42 KiB. Keep the BLE scan exclusive with HTTPS via NetworkBudgetLease,
// and retain a 64-KiB free + 20-KiB contiguous floor before every real scan.
// This is a scan-admission limit, NOT a reduction of TLS/OTA memory guards.
constexpr size_t ble_scan_min_internal_bytes=64u*1024u;
constexpr size_t ble_scan_min_contiguous_bytes=20u*1024u;
constexpr bool internal_memory_admissible(
    size_t free_internal, size_t contiguous_internal,
    size_t minimum_free, size_t minimum_contiguous) {
    return free_internal>=minimum_free &&
           contiguous_internal>=minimum_contiguous;
}
} // namespace enevina::cottage
