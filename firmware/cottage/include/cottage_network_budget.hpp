#pragma once
// C2.4: limit concurrent internal-RAM-heavy TLS and BLE work.
// There is exactly one instance in cottage_network_budget.cpp.
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace enevina_firmware {
enum class NetworkBudgetFailure : uint8_t { None=0, MutexBusy=1, LowMemory=2 };
// Capture refusal cause and (when available) internal memory atomically
// while the shared gate is held. Never infer the cause from heap sampled
// after the gate has been released to another TLS/BLE task.
struct NetworkBudgetDiagnostic {
    NetworkBudgetFailure failure=NetworkBudgetFailure::None;
    uint32_t free_internal=0;
    uint32_t largest_internal=0;
};
bool network_budget_init();
bool network_budget_acquire(uint32_t wait_ms, size_t min_internal_bytes,
                            size_t min_contiguous_bytes,
                            NetworkBudgetDiagnostic* diagnostic=nullptr);
void network_budget_release();
uint32_t network_budget_denials();
void network_budget_sample();
uint32_t network_budget_min_largest_internal();

class NetworkBudgetLease final {
public:
    NetworkBudgetLease(uint32_t wait_ms, size_t min_internal, size_t min_largest)
        : held_(network_budget_acquire(wait_ms,min_internal,min_largest,
                                      &diagnostic_)) {}
    ~NetworkBudgetLease() { if(held_) network_budget_release(); }
    NetworkBudgetLease(const NetworkBudgetLease&)=delete;
    NetworkBudgetLease& operator=(const NetworkBudgetLease&)=delete;
    bool held() const { return held_; }
    const NetworkBudgetDiagnostic& diagnostic() const { return diagnostic_; }
private:
    // Declare diagnostic before held_: it must exist before acquisition
    // writes the failure/success snapshot.
    NetworkBudgetDiagnostic diagnostic_{};
    bool held_;
};
}  // namespace enevina_firmware
