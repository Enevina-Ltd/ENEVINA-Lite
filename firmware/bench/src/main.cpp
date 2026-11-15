// Isolated serial-only benchmark. No network, pump adapter, or command path.
#include <Arduino.h>
#include <cmath>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "physics/physics.hpp"
#include "optimizer/optimizer.hpp"

// PlatformIO compiles this single translation unit from the repository root.
// Keeping the exact production sources here makes a board result comparable.
#include "../../../physics/physics.cpp"
#include "../../../optimizer/optimizer.cpp"

namespace {
enevina::Period periods[enevina::max_periods];

enevina::Parameters parameters(int profile) {
    enevina::Parameters p{};
    p.building_capacity_kwh_per_c=profile==1 ? 8 : 12;
    p.building_loss_kw_per_c=profile==1 ? 0.24 : 0.18;
    p.tank_capacity_kwh=8;
    p.tank_loss_kw_per_kwh=0.002;
    p.pump_max_thermal_kw=8;
    p.pump_derate_kw_per_c_below_zero=0.1;
    p.cop_at_reference=3.4;
    p.cop_outdoor_per_c=0.055;
    p.cop_supply_per_c=0.025;
    p.minimum_cop=1.2;
    p.maximum_cop=5;
    p.emitter_kw_per_c=0.8;
    p.dhw_supply_c=50;
    p.indoor_min_c=19;
    p.indoor_max_c=21;
    p.tank_min_kwh=2;
    if(profile==2) {
        p.building_capacity_kwh_per_c=10;
        p.building_loss_kw_per_c=0.21;
        p.pump_max_thermal_kw=7;
        p.pump_derate_kw_per_c_below_zero=0.07;
        p.cop_at_reference=3;
        p.cop_outdoor_per_c=0.07;
        p.cop_supply_per_c=0.035;
        p.emitter_kw_per_c=0.36;
        p.space_supply_high_c=50;
        p.dhw_supply_c=52;
    }
    return p;
}

int64_t run(int profile, size_t count, int repetition) {
    const enevina::Parameters p=parameters(profile);
    constexpr double pi=3.141592653589793;
    constexpr double radiator_prices[]={0.12,0.50,0.06,0.28};
    for(size_t i=0;i<count;++i) {
        const double outdoor=profile==2 ?
            -2+4*std::sin(2*pi*i/96) :
            (profile==1 ? -4 : 2)+2*std::sin(2*pi*i/96);
        const double price=profile==2 ? radiator_prices[(i%32)/8] :
            (i%48<16 ? 0.08 : 0.42)+(i%12<4 ? 0.02 : 0.0);
        const double gain= (i%96>=28 && i%96<68) ?
            (profile==2 ? 0.15 : 0.2) : 0.0;
        const double draw=(i%24==7 || i%24==8 || i%24==20) ? 2.0 : 0.0;
        periods[i]={outdoor,price,gain,draw,0};
    }
    const size_t internal_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t internal_low_before=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const size_t psram_before=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t psram_low_before=heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    const int64_t start=esp_timer_get_time();
    const auto result=enevina::optimize(p,{20,5},periods,count);
    const int64_t elapsed_ms=(esp_timer_get_time()-start)/1000;
    Serial.printf("BENCH profile=%d slots=%u repetition=%d status=%d cost=%.6f ms=%lld internal_before=%u internal_after=%u internal_low_before=%u internal_low_after=%u psram_before=%u psram_after=%u psram_low_before=%u psram_low_after=%u\n",
        profile,static_cast<unsigned>(count),repetition,
        static_cast<int>(result.status),result.cost_eur,
        static_cast<long long>(elapsed_ms),static_cast<unsigned>(internal_before),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(internal_low_before),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(psram_before),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        static_cast<unsigned>(psram_low_before),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));
    return elapsed_ms;
}
}

void setup() {
    Serial.begin(115200);
    delay(2500);
    Serial.printf("ENEVINA_BENCH chip=%s psram=%u\n",ESP.getChipModel(),
        static_cast<unsigned>(ESP.getPsramSize()));
    const BaseType_t started=xTaskCreatePinnedToCore([](void*) {
        for(int profile=0;profile<3;++profile) {
            for(size_t count : {size_t(96),size_t(192)}) {
                int64_t total=0,worst=0;
                for(int repetition=1;repetition<=3;++repetition) {
                    const int64_t ms=run(profile,count,repetition);
                    total+=ms;
                    if(ms>worst) worst=ms;
                    delay(50);
                }
                Serial.printf("BENCH_SUMMARY profile=%d slots=%u average_ms=%.1f worst_ms=%lld\n",
                    profile,static_cast<unsigned>(count),double(total)/3,
                    static_cast<long long>(worst));
            }
        }
        Serial.printf("BENCH task_stack_min_free=%u\n",
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        Serial.println("ENEVINA_BENCH_DONE");
        vTaskDelete(nullptr);
    },"enevina_benchmark",32768,nullptr,1,nullptr,1);
    if(started!=pdPASS) Serial.println("ENEVINA_BENCH_TASK_FAILED");
}
void loop() { delay(1000); }
