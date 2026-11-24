#pragma once
#include <memory>
#include <new>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif
namespace enevina_firmware {
template<class T> struct ForecastFree {
    void operator()(T* p) const {
        if(!p)return;
#ifdef ARDUINO
        p->~T();heap_caps_free(p);
#else
        delete p;
#endif
    }
};
template<class T> using ForecastBuffer=std::unique_ptr<T,ForecastFree<T>>;
template<class T> ForecastBuffer<T> forecast_buffer() {
#ifdef ARDUINO
    void* mem=heap_caps_malloc(sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return ForecastBuffer<T>(mem?new(mem) T{}:nullptr);
#else
    return ForecastBuffer<T>(new(std::nothrow) T{});
#endif
}
}
