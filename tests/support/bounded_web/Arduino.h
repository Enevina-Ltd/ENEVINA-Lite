#pragma once
#include <cstdint>
inline uint32_t fake_clock=0;
inline uint32_t millis(){return fake_clock;}
inline void delay(uint32_t ms){fake_clock+=ms;}
