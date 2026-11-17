#pragma once
#include <cstddef>
namespace enevina::cottage {
// BLE and TLS share the same internal heap but never perform expensive work
// concurrently. Keep a margin above the HTTPS floor without starving BLE
// whenever the steady-state ESP32 has less than the former 96 KiB threshold.
constexpr size_t ble_scan_min_internal_bytes=80u*1024u;
constexpr size_t ble_scan_min_contiguous_bytes=20u*1024u;
constexpr bool internal_memory_admissible(
    size_t free_internal, size_t contiguous_internal,
    size_t minimum_free, size_t minimum_contiguous) {
    return free_internal>=minimum_free &&
           contiguous_internal>=minimum_contiguous;
}
} // namespace enevina::cottage
