#include "cottage_tls_memory.hpp"
#include "cottage/tls_record_memory_policy.hpp"
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl_internal.h>
#include <atomic>

namespace enevina_firmware {
namespace {
std::atomic<bool> enabled{false};
std::atomic<TaskHandle_t> scope_owner{nullptr};
std::atomic<uint8_t> scope_source{0};
std::atomic<uint32_t> allocations[4]{};
std::atomic<uint32_t> failures{0};
constexpr size_t input_bytes=MBEDTLS_SSL_IN_BUFFER_LEN;
constexpr size_t output_bytes=MBEDTLS_SSL_OUT_BUFFER_LEN;
static_assert(input_bytes>=16u*1024u && output_bytes>=16u*1024u,
              "Review the TLS record allocation policy when changing SDK");
void* tls_calloc(size_t n,size_t size) {
    const bool scoped=scope_owner.load()==xTaskGetCurrentTaskHandle() &&
                      scope_owner.load()!=nullptr;
    if(!enevina::cottage::tls_record_use_psram(scoped,n,size,input_bytes,output_bytes))
        return heap_caps_calloc(n,size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    // No fallback into the scarce internal heap. ESP-IDF supports external
    // mbedTLS allocations; only large record buffers of these data readers
    // use it. Wi-Fi, RTOS, stacks and other crypto allocations are unchanged.
    auto* p=heap_caps_calloc(n,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    const auto source=scope_source.load();
    if(p && source>0 && source<4)allocations[source].fetch_add(1);
    if(!p)failures.fetch_add(1);
    return p;
}
void tls_free(void* ptr){heap_caps_free(ptr);}
}
bool cottage_tls_memory_init() {
    if(enabled.load())return true;
    const bool ok=mbedtls_platform_set_calloc_free(tls_calloc,tls_free)==0;
    enabled.store(ok);
    return ok;
}
bool cottage_tls_memory_enabled(){return enabled.load();}
uint32_t cottage_tls_psram_allocations(PublicTlsSource source) {
    return allocations[static_cast<uint8_t>(source)].load();
}
uint32_t cottage_tls_psram_failures(){return failures.load();}
uint32_t cottage_tls_record_size(){return static_cast<uint32_t>(input_bytes);}
PublicTlsMemoryScope::PublicTlsMemoryScope(PublicTlsSource source) {
    if(!enabled.load())return;
    const auto self=xTaskGetCurrentTaskHandle();
    TaskHandle_t empty=nullptr;
    if(self && scope_owner.compare_exchange_strong(empty,self)) {
        scope_source.store(static_cast<uint8_t>(source));
        owner_=self;
    }
}
void PublicTlsMemoryScope::end() {
    if(!owner_)return;
    scope_source.store(0);
    scope_owner.store(nullptr);
    owner_=nullptr;
}
}
