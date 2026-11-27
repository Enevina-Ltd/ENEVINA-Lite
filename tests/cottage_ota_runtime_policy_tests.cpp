#include "cottage/ota_runtime_policy.hpp"
#include <cassert>
using namespace enevina::cottage;
int main() {
    assert(ota_manifest_min_internal_bytes==72u*1024u);
    assert(ota_image_min_internal_bytes==76u*1024u);
    assert(ota_min_contiguous_internal_bytes==20u*1024u);
    assert(ota_inflight_min_internal_bytes==24u*1024u);
    assert(ota_readers_release_ms>=1500u);
    assert(ota_flash_idle_delay_ticks>=1u);
    assert(ota_download_progress_step_bytes==64u*1024u);
    assert(!ota_progress_expired(600000u,599000u)); // slow but advancing
    assert(!ota_progress_expired(ota_no_progress_ms-1,0));
    assert(ota_progress_expired(ota_no_progress_ms,0));
    assert(!ota_progress_expired(50,0xfffffff0u)); // millis wrap
    assert(ota_progress_expired(ota_no_progress_ms,0xfffffff0u));
    assert(ota_range_bytes==32768u);
}
