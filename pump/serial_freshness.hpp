#pragma once
#include <cstdint>

namespace enevina {
// Transport reachability is not pump telemetry freshness. HeishaMon's HTTP
// /json?topic= endpoint may replay cached values indefinitely. Only an
// independent pump-side successful serial-read counter can renew freshness.
struct PumpFreshnessEvidence {
    bool http_sample_ok=false;
    bool counter_available=false;
    uint64_t previous_good_reads=0;
    uint64_t current_good_reads=0;
};
inline bool confirmed_pump_serial_update(const PumpFreshnessEvidence& e) {
    return e.http_sample_ok && e.counter_available &&
           e.current_good_reads>e.previous_good_reads;
}
} // namespace enevina
