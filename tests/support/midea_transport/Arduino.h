#pragma once
#include <cstdint>
#include <cstddef>
namespace midea_test { inline uint32_t now_ms=0,link_generation=1,endpoint_generation=1; }
inline unsigned long millis(){return midea_test::now_ms;}
inline void delay(unsigned long ms){midea_test::now_ms+=static_cast<uint32_t>(ms);}
inline void vTaskDelay(unsigned long ms){delay(ms);}
#define pdMS_TO_TICKS(x) (x)
