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
    // Contract, NOT an arbitrary/random boot token: a persistent,
    // strictly increasing source UART-session generation. Unknown contract
    // must remain UNVERIFIED_SOURCE at the adapter boundary.
    uint64_t boot_epoch=0;
    uint64_t successful_uart_reads=0;
    int64_t source_utc=0;
    int64_t received_utc=0;
};
class PumpReadCounterTracker final {
public:
    void invalidate() { needs_new_baseline_=true; } // keep epoch high-water
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
        if(msg.boot_epoch==0) {
            invalidate();
            return PumpReadProofStatus::Unverified;
        }
        if(!have_baseline_) {
            update_baseline(msg);
            return PumpReadProofStatus::Baseline;
        }
        if(msg.boot_epoch!=epoch_) {
            // Replayed earlier boot epochs must NEVER replace the current
            // epoch and erase its high-water counter. A random boot ID does
            // not satisfy this monotonic generation contract.
            // The new session must also have a strictly newer SOURCE time.
            if(msg.boot_epoch<epoch_ || msg.source_utc<=max_source_utc_) {
                invalidate();
                return PumpReadProofStatus::CounterReset;
            }
            update_baseline(msg);
            return PumpReadProofStatus::Baseline;
        }
        // Across this tracked session, a later counter with an earlier
        // source timestamp is a reordered/cached packet, not a fresh poll.
        if(msg.source_utc<max_source_utc_)
            return PumpReadProofStatus::Replay;
        if(msg.successful_uart_reads<counter_) {
            // A lower counter in the SAME epoch may be an old replay.
            // Do not decrease the high-water mark without a new boot epoch.
            return PumpReadProofStatus::CounterReset;
        }
        if(needs_new_baseline_) {
            // After transport loss, the first snapshot is never certified.
            counter_=msg.successful_uart_reads;
            if(msg.source_utc>max_source_utc_)max_source_utc_=msg.source_utc;
            needs_new_baseline_=false;
            return PumpReadProofStatus::Baseline;
        }
        if(msg.successful_uart_reads==counter_)
            return PumpReadProofStatus::Replay;
        counter_=msg.successful_uart_reads;
        if(msg.source_utc>max_source_utc_)max_source_utc_=msg.source_utc;
        return PumpReadProofStatus::NewRead;
    }
private:
    void update_baseline(const PumpReadCounterMessage& msg) {
        epoch_=msg.boot_epoch;
        counter_=msg.successful_uart_reads;
        if(msg.source_utc>max_source_utc_)max_source_utc_=msg.source_utc;
        have_baseline_=true;
        needs_new_baseline_=false;
    }
    bool have_baseline_=false;
    bool needs_new_baseline_=false;
    uint64_t epoch_=0,counter_=0;
    int64_t max_source_utc_=0; // never reduced across replay/reconnect
};
} // namespace enevina
