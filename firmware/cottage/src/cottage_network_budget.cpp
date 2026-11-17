#include "cottage_network_budget.hpp"
#include "cottage/memory_policy.hpp"
#include <limits>
#include <esp_heap_caps.h>
#include <atomic>

namespace enevina_firmware {
namespace {
SemaphoreHandle_t gate=nullptr;
// Count refusals rather than hiding failed allocations or heap minima.
std::atomic<uint32_t> refusals{0};
std::atomic<uint32_t> min_largest{std::numeric_limits<uint32_t>::max()};
}
bool network_budget_init() {
    if(!gate) gate=xSemaphoreCreateMutex();
    return gate!=nullptr;
}
bool network_budget_acquire(uint32_t wait_ms,size_t min_internal,
                            size_t min_largest) {
    if(!gate || xSemaphoreTake(gate,pdMS_TO_TICKS(wait_ms))!=pdTRUE) {
        refusals.fetch_add(1);return false;
    }
    // MUST use internal 8-bit-capable RAM, not ESP.getFreeHeap() which may
    // include PSRAM. The TLS/BT stack requires internal allocations.
    const size_t free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    const size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    network_budget_sample();
    if(!enevina::cottage::internal_memory_admissible(free,largest,
                                                      min_internal,min_largest)) {
        refusals.fetch_add(1);
        xSemaphoreGive(gate);
        return false;
    }
    return true;
}
void network_budget_release() {
    network_budget_sample();
    if(gate) xSemaphoreGive(gate);
}
void network_budget_sample() {
    const auto current=static_cast<uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    auto observed=min_largest.load();
    while(current<observed &&
          !min_largest.compare_exchange_weak(observed,current)) {}
}
uint32_t network_budget_min_largest_internal() {
    const uint32_t v=min_largest.load();
    return v==std::numeric_limits<uint32_t>::max()?0:v;
}
uint32_t network_budget_denials() {return refusals.load();}
}  // namespace enevina_firmware
