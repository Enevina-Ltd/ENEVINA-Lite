#include "cottage_network_budget.hpp"
#include <esp_heap_caps.h>
#include <atomic>

namespace enevina_firmware {
namespace {
SemaphoreHandle_t gate=nullptr;
// Count refusals rather than hiding failed allocations or heap minima.
std::atomic<uint32_t> refusals{0};
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
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<min_internal ||
       heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<min_largest) {
        refusals.fetch_add(1);
        xSemaphoreGive(gate);
        return false;
    }
    return true;
}
void network_budget_release() {
    if(gate) xSemaphoreGive(gate);
}
uint32_t network_budget_denials() {return refusals.load();}
}  // namespace enevina_firmware
