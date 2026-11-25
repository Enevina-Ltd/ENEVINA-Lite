#pragma once
#include <cstddef>
#include <limits>

namespace enevina::cottage {
// Only a scoped public-data TLS session may move the pinned mbedTLS
// record buffers to PSRAM. Keep all other allocations on the SDK path.
constexpr bool tls_record_use_psram(bool scoped,size_t n,size_t size,
                                    size_t input_bytes,size_t output_bytes) {
    if(!scoped || !n || !size || n>std::numeric_limits<size_t>::max()/size)
        return false;
    const auto bytes=n*size;
    return bytes>=16u*1024u && (bytes==input_bytes || bytes==output_bytes);
}
}
