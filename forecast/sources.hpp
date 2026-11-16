#pragma once
#include "forecast/pricing.hpp"
#include <cstddef>
#include <cstdint>

namespace enevina {
// Nord Pool Data Portal's FI values are EUR/MWh excluding VAT. A missing
// value is never represented by zero. Intervals use UTC seconds [start,end).
struct MarketPrice {
    int64_t start_utc=0;
    int64_t end_utc=0;
    double eur_per_mwh=0;
};
struct PriceSlot {
    int64_t start_utc=0;
    double total_eur_per_kwh=0;
};
// Accepts only the source's explicit UTC Z timestamps, including fractional
// seconds; offset/local strings must be normalized before this boundary.
bool parse_utc_z(const char* text, int64_t& epoch_seconds);
// Fails closed on gaps, overlaps, duplicates, wrong duration or bad values.
bool normalize_nordpool_fi(const MarketPrice* source, size_t count,
    const Tariff& local_tariff, PriceSlot* output, size_t capacity);
}
