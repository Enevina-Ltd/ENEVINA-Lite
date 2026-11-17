#pragma once
#include <cmath>
#include <cstdint>
#include "cottage/telemetry.hpp"
#include "pump/serial_freshness.hpp"

namespace enevina::cottage {
// Optional model-specific CN105 proof message. An upstream bridge must create
// one atomic snapshot after successful, checksum-valid, decoded 0x02/0x03/0x06
// UART replies. A cached SSE value, heartbeat or HTTP receive is never proof.
struct CN105ProvenPoll {
    uint32_t schema=0;
    uint64_t boot_epoch=0;
    uint64_t completed_poll_sequence=0;
    int64_t oldest_uart_read_utc=0; // first of the three source UART replies
    int64_t latest_uart_read_utc=0; // last source UART reply
    int64_t received_utc=0; // assigned locally by Lite, never by bridge
    bool uart_connected=false;
    bool complete_decoded_poll=false;
    double indoor_c=0;
    double setpoint_c=0;
    double compressor_hz=0;
    ObservedPumpMode mode=ObservedPumpMode::Unknown;
};
class CN105SourceProofVerifier final {
public:
    void reconnect() { tracker_.invalidate(); }
    enevina::PumpReadProofStatus accept(const CN105ProvenPoll& in,
                                        CN105Telemetry& out) {
        if(in.schema!=1 || in.boot_epoch==0 ||
           in.boot_epoch>UINT32_MAX || in.completed_poll_sequence==0 ||
           in.completed_poll_sequence>UINT32_MAX ||
           !in.uart_connected || !in.complete_decoded_poll ||
           in.mode==ObservedPumpMode::Unknown ||
           !std::isfinite(in.indoor_c) ||
           !std::isfinite(in.setpoint_c) ||
           !std::isfinite(in.compressor_hz) ||
           in.indoor_c < -10 || in.indoor_c > 50 ||
           in.setpoint_c < 0 || in.setpoint_c > 35 ||
           in.compressor_hz < 0 || in.compressor_hz > 200) {
            tracker_.invalidate();
            return enevina::PumpReadProofStatus::Unverified;
        }
        if(in.oldest_uart_read_utc<=0 || in.latest_uart_read_utc<=0 ||
           in.received_utc<=0 ||
           in.oldest_uart_read_utc>in.latest_uart_read_utc ||
           in.latest_uart_read_utc>in.received_utc) {
            tracker_.invalidate();
            return enevina::PumpReadProofStatus::InvalidClock;
        }
        // A very old setting mixed with a fresh compressor value is NOT an
        // atomic completed poll, even when the completion seq increments.
        if(in.latest_uart_read_utc-in.oldest_uart_read_utc>60) {
            tracker_.invalidate();
            return enevina::PumpReadProofStatus::StaleSource;
        }
        const auto verdict=tracker_.observe({
            true,in.uart_connected,in.complete_decoded_poll,true,
            in.boot_epoch,in.completed_poll_sequence,
            in.oldest_uart_read_utc,in.received_utc},in.received_utc,180);
        if(verdict!=enevina::PumpReadProofStatus::NewRead) return verdict;
        CN105Telemetry validated{};
        validated.received=true;
        validated.captured_utc=in.oldest_uart_read_utc;
        validated.indoor_c=in.indoor_c;
        validated.setpoint_c=in.setpoint_c;
        validated.compressor_hz=in.compressor_hz;
        validated.mode=in.mode;
        validated.evidence={in.received_utc,in.oldest_uart_read_utc,true};
        if(!valid_cn105(validated,in.received_utc)) {
            tracker_.invalidate();
            return enevina::PumpReadProofStatus::Unverified;
        }
        out=validated;
        return enevina::PumpReadProofStatus::NewRead;
    }
private:
    enevina::PumpReadCounterTracker tracker_{};
};
} // namespace enevina::cottage
