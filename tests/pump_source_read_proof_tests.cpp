#include "pump/serial_freshness.hpp"
#include "pump/measurement_quality.hpp"
#include <cstdlib>
#include <cstdio>
using namespace enevina;
#define CHECK(e) do { if(!(e)) {std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#e);std::exit(1);} } while(false)
int main() {
    PumpReadCounterTracker t;
    const int64_t now=1800000000;
    PumpReadCounterMessage r{true,true,true,true,7,99,now-3,now};
    CHECK(t.observe(r,180)==PumpReadProofStatus::Baseline);
    // A successful new UART reply need not change the numeric value.
    r.successful_uart_reads=100;
    CHECK(t.observe(r,180)==PumpReadProofStatus::NewRead);
    r.successful_uart_reads=101;
    CHECK(t.observe(r,180)==PumpReadProofStatus::NewRead);
    CHECK(t.observe(r,180)==PumpReadProofStatus::Replay); // cached/replayed SSE
    t.invalidate(); // SSE reconnect: never certify cached initial snapshot
    CHECK(t.observe(r,180)==PumpReadProofStatus::Baseline);
    ++r.successful_uart_reads;
    CHECK(t.observe(r,180)==PumpReadProofStatus::NewRead);
    r.uart_connected=false; // serial cable lost while SSE still reachable
    CHECK(t.observe(r,180)==PumpReadProofStatus::Unverified);
    r.uart_connected=true;
    CHECK(t.observe(r,180)==PumpReadProofStatus::Baseline);
    r.successful_uart_reads=1; // counter reset
    CHECK(t.observe(r,180)==PumpReadProofStatus::CounterReset);
    ++r.successful_uart_reads;
    CHECK(t.observe(r,180)==PumpReadProofStatus::NewRead);
    ++r.boot_epoch; // new firmware session, source restart
    CHECK(t.observe(r,180)==PumpReadProofStatus::Baseline);
    ++r.successful_uart_reads;
    r.source_utc=now+1; // invalid source clock
    CHECK(t.observe(r,180)==PumpReadProofStatus::InvalidClock);
    r.source_utc=now-181; // stale even if counter increments
    CHECK(t.observe(r,180)==PumpReadProofStatus::StaleSource);
    r.source_utc=now;
    r.counter_bound_to_sample=false;
    CHECK(t.observe(r,180)==PumpReadProofStatus::Unverified);
    r.counter_bound_to_sample=true;
    r.documented_uart_counter=false; // uptime must not masquerade as read seq
    CHECK(t.observe(r,180)==PumpReadProofStatus::Unverified);
    CHECK(!confirmed_pump_serial_update({true,true,5,5}));
    CHECK(assess_measurement({now,now-181,true},now,180)==MeasurementQuality::StaleSource);
    CHECK(assess_measurement({now,0,false},now,180)==MeasurementQuality::UnverifiedSource);
    std::puts("PASS source read counter, replay/reconnect, reset, UART loss and clocks");
}
