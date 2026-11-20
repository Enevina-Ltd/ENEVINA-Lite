#pragma once
#include <cstddef>
#include <cstdint>

namespace enevina_firmware {
// Unit note: values named *_eur_* are in the CURRENCY of the configured
// region (EUR for most zones, SEK/NOK/DKK/PLN for the zones that settle in
// them). The identifiers predate regional pricing; `currency` is the truth.
// Unit note: values named *_eur_* are in the CURRENCY of the configured
// region (EUR for most zones, SEK/NOK/DKK/PLN for the zones that settle in
// them). The identifiers predate regional pricing; `currency` is the truth.
struct MarketStatus {
    bool clock_trusted=false;
    bool current_price_valid=false;
    double current_spot_eur_per_kwh=0;
    int64_t valid_until_utc=0;
    size_t slots=0;
    int64_t last_fetch_utc=0;
    char area[8]="FI";      // Nord Pool deliveryArea of the configured zone
    char currency[4]="EUR"; // ISO 4217 of every price value
    uint8_t last_error=0;   // enevina::DayAheadError (0 OK) or 101..104 transport
    int last_http_status=0;
    int64_t last_attempt_utc=0;
};
struct PricePoint {
    int64_t start_utc=0;
    double spot_eur_per_kwh=0;
};
void start_market_reader();
uint32_t market_reader_stack_free();
// Select the bidding zone and currency prices are fetched for (Nord Pool
// deliveryArea such as "SE3", ISO 4217 such as "SEK"). nullptr/nullptr means
// "no zone chosen yet": nothing is fetched. A change discards the old zone's
// cache. Safe to call before or after start_market_reader().
bool market_set_region(const char* area,const char* currency);
// Select the bidding zone and currency prices are fetched for (Nord Pool
// deliveryArea such as "SE3", ISO 4217 such as "SEK"). Safe to call before or
// after start_market_reader(); a change discards the old zone's cache.
bool market_set_region(const char* area,const char* currency);
MarketStatus market_status();
size_t future_prices(PricePoint* output, size_t capacity);
}
