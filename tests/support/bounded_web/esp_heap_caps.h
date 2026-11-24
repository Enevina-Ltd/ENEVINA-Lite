#pragma once
#include <cstdlib>
#include <cstddef>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
inline size_t ingress_allocations=0;
inline bool fail_ingress_allocation=false;
inline void* heap_caps_malloc(size_t bytes,int flags) {
    if(flags!=(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)||fail_ingress_allocation)return nullptr;
    auto* p=std::malloc(bytes);if(p)++ingress_allocations;return p;
}
inline void heap_caps_free(void* p){if(p){--ingress_allocations;std::free(p);}}
