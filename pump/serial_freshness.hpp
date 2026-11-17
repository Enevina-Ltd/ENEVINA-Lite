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
// Optional strict counter contract for a future CN105/other model adapter.
// Counter is *not* connection uptime: it must advance only on an actual
// successfully decoded pump UART reply, and be atomic with the sample.
// This alone does not fabricate a source timestamp from HTTP/SSE receipt.
enum class PumpReadProofStatus : uint8_t {
    Unverified, Baseline, NewRead, Replay, CounterReset,
    InvalidClock, StaleSource
};
struct PumpReadCounterMessage {
    bool documented_uart_counter=false;
    bool uart_connected=false;
    bool packet_decoded=false;
    bool counter_bound_to_sample=false;
    uint64_t boot_epoch=0;
    uint64_t successful_uart_reads=0;
    int64_t source_utc=0;
    int64_t received_utc=0;
};
class PumpReadCounterTracker final {
public:
    void invalidate() { have_baseline_=false; }
    PumpReadProofStatus observe(const PumpReadCounterMessage& msg,
                               int64_t now_utc,int64_t max_age_s) {
        if(!msg.documented_uart_counter || !msg.uart_connected ||
           !msg.packet_decoded || !msg.counter_bound_to_sample) {
            invalidate();
            return PumpReadProofStatus::Unverified;
        }
        if(msg.source_utc<=0 || msg.received_utc<=0 ||
           now_utc<=0 || max_age_s<=0 ||
           msg.source_utc>msg.received_utc ||
           msg.received_utc>now_utc) {
            invalidate();
            return PumpReadProofStatus::InvalidClock;
        }
        // The upstream timestamps may be internally consistent but hours
        // old: a cached SSE payload must be compared with current UTC.
        if(now_utc-msg.source_utc>max_age_s) {
            invalidate();
            return PumpReadProofStatus::StaleSource;
        }
        if(!have_baseline_ || msg.boot_epoch!=epoch_) {
            update_baseline(msg);
            return PumpReadProofStatus::Baseline;
        }
        if(msg.successful_uart_reads<counter_) {
            update_baseline(msg); // after reset, require another real increment
            return PumpReadProofStatus::CounterReset;
        }
        if(msg.successful_uart_reads==counter_)
            return PumpReadProofStatus::Replay;
        counter_=msg.successful_uart_reads;
        return PumpReadProofStatus::NewRead;
    }
private:
    void update_baseline(const PumpReadCounterMessage& msg) {
        epoch_=msg.boot_epoch;
        counter_=msg.successful_uart_reads;
        have_baseline_=true;
    }
    bool have_baseline_=false;
    uint64_t epoch_=0,counter_=0;
};
} // namespace enevina
