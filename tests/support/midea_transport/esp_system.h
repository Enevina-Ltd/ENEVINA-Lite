#pragma once
#include <cstring>
inline void esp_fill_random(void* out,size_t n){std::memset(out,0x35,n);}
