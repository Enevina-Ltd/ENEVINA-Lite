#pragma once
#include <cstddef>
namespace enevina::cottage {
constexpr bool internal_memory_admissible(
    size_t free_internal, size_t contiguous_internal,
    size_t minimum_free, size_t minimum_contiguous) {
    return free_internal>=minimum_free &&
           contiguous_internal>=minimum_contiguous;
}
} // namespace enevina::cottage
