#pragma once
#include <cstddef>
#include <cstdint>

namespace enevina_firmware {
struct MarketStatus {
    bool clock_trusted=false;
    bool current_price_valid=false;
    double current_spot_eur_per_kwh=0;
    int64_t valid_until_utc=0;
    size_t slots=0;
    int64_t last_fetch_utc=0;
};
struct PricePoint {
    int64_t start_utc=0;
    double spot_eur_per_kwh=0;
};
void start_market_reader();
MarketStatus market_status();
size_t future_prices(PricePoint* output, size_t capacity);
}
