#pragma once
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace enevina_firmware {
enum class PublicTlsSource : uint8_t {Market=1, Weather=2, Ota=3};
// Install once, before Wi-Fi and before any network workers start.
// Existing SDK allocations can be freed by heap_caps_free in either arena.
bool cottage_tls_memory_init();
bool cottage_tls_memory_enabled();
uint32_t cottage_tls_psram_allocations(PublicTlsSource source);
uint32_t cottage_tls_psram_failures();
uint32_t cottage_tls_record_size();
// Existing shared network budget serializes these public TLS sessions.
// Scope is task-specific: unrelated crypto/credentials keep SDK allocation.
class PublicTlsMemoryScope final {
public:
    explicit PublicTlsMemoryScope(PublicTlsSource source);
    ~PublicTlsMemoryScope(){end();}
    void end();
    PublicTlsMemoryScope(const PublicTlsMemoryScope&)=delete;
    PublicTlsMemoryScope& operator=(const PublicTlsMemoryScope&)=delete;
private:
    TaskHandle_t owner_=nullptr;
};
}
