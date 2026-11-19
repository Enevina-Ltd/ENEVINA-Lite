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
constexpr uint32_t ota_readers_release_ms=1800u;
constexpr uint32_t ota_flash_idle_delay_ticks=2u;
constexpr uint32_t ota_download_progress_step_bytes=64u*1024u;

static_assert(ota_manifest_min_internal_bytes<=ota_image_min_internal_bytes);
static_assert(ota_inflight_min_internal_bytes<ota_image_min_internal_bytes);
static_assert(ota_flash_idle_delay_ticks>=1u);
}
