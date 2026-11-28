#pragma once
#include <cstddef>
#include <cstdint>

// OTA must leave idle tasks runnable while it hashes and writes flash.
// A taskYIELD() loop at task priority 1 does NOT guarantee the idle task
// gets scheduled; actual delay ticks do.
namespace enevina::cottage {
constexpr uint32_t ota_manifest_min_internal_bytes=72u*1024u;
constexpr uint32_t ota_image_min_internal_bytes=76u*1024u;
constexpr uint32_t ota_min_contiguous_internal_bytes=20u*1024u;
constexpr uint32_t ota_inflight_min_internal_bytes=24u*1024u;
// Allow a temporary BLE/Wi-Fi allocation to drain before the next image
// read/write. Never perform OTA I/O while the original minimum is violated.
constexpr unsigned ota_inflight_recheck_max_waits=8u;
constexpr uint32_t ota_inflight_recheck_delay_ms=40u;
constexpr uint32_t ota_readers_release_ms=1800u;
// Give the image lease a bounded chance to cross a BLE scan or a short-lived
// TLS reader. Failure does not lower the original 76 KiB / 20 KiB gates.
constexpr unsigned ota_image_lease_max_attempts=12u;
constexpr uint32_t ota_image_lease_retry_delay_ms=400u;
constexpr uint32_t ota_flash_idle_delay_ticks=2u;
constexpr uint32_t ota_download_progress_step_bytes=64u*1024u;
// Stage the entire signed image in external RAM, never in scarce internal
// heap. Keep PSRAM for sensors/UI. Bound inactivity, not transfer duration.
constexpr uint32_t ota_staging_psram_reserve_bytes=512u*1024u;
constexpr uint32_t ota_no_progress_ms=180000u;
constexpr uint32_t ota_range_bytes=32u*1024u;
constexpr uint32_t ota_retry_delay_ms=1000u;
constexpr uint32_t ota_reader_window_ms=500u;
constexpr uint32_t ota_range_lease_slice_ms=15000u;
// Unsigned subtraction remains correct across the millis() rollover.
constexpr bool ota_progress_expired(uint32_t now,uint32_t last_progress) {
    return static_cast<uint32_t>(now-last_progress)>=ota_no_progress_ms;
}
constexpr uint32_t ota_flash_chunk_bytes=2048u;

static_assert(ota_manifest_min_internal_bytes<=ota_image_min_internal_bytes);
static_assert(ota_inflight_min_internal_bytes<ota_image_min_internal_bytes);
static_assert(ota_flash_idle_delay_ticks>=1u);
static_assert(ota_inflight_recheck_max_waits>=2u &&
              ota_inflight_recheck_max_waits<=12u);
static_assert(ota_inflight_recheck_max_waits*ota_inflight_recheck_delay_ms<=500u);
static_assert(ota_image_lease_max_attempts>=2 && ota_image_lease_max_attempts<=16);
static_assert(ota_image_lease_retry_delay_ms>=100 &&
              ota_image_lease_retry_delay_ms<=1000);
}
