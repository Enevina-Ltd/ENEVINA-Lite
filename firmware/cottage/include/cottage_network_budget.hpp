#pragma once
// C2.4: limit concurrent internal-RAM-heavy TLS and BLE work.
// There is exactly one instance in cottage_network_budget.cpp.
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace enevina_firmware {
bool network_budget_init();
bool network_budget_acquire(uint32_t wait_ms, size_t min_internal_bytes,
                            size_t min_contiguous_bytes);
void network_budget_release();
uint32_t network_budget_denials();
void network_budget_sample();
uint32_t network_budget_min_largest_internal();

class NetworkBudgetLease final {
public:
    NetworkBudgetLease(uint32_t wait_ms, size_t min_internal, size_t min_largest)
        : held_(network_budget_acquire(wait_ms,min_internal,min_largest)) {}
    ~NetworkBudgetLease() { if(held_) network_budget_release(); }
    NetworkBudgetLease(const NetworkBudgetLease&)=delete;
    NetworkBudgetLease& operator=(const NetworkBudgetLease&)=delete;
    bool held() const { return held_; }
private:
    bool held_;
};
}  // namespace enevina_firmware
