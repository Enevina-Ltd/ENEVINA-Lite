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
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::Baseline);
    // A successful new UART reply need not change the numeric value.
    r.successful_uart_reads=100;
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::NewRead);
    r.successful_uart_reads=101;
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::NewRead);
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::Replay); // cached/replayed SSE
    t.invalidate(); // SSE reconnect: never certify cached initial snapshot
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::Baseline);
    ++r.successful_uart_reads;
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::NewRead);
    r.uart_connected=false; // serial cable lost while SSE still reachable
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::Unverified);
    r.uart_connected=true;
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::Baseline);
    r.successful_uart_reads=1; // same epoch lower counter: never reduce high-water
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::CounterReset);
    ++r.successful_uart_reads;
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::CounterReset);
    ++r.boot_epoch; // documented monotonic epoch plus strictly newer source read
    r.source_utc=now-2;
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::Baseline);
    ++r.successful_uart_reads;
    r.source_utc=now+1; // invalid source clock
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::InvalidClock);
    r.source_utc=now-181; // stale even if counter increments
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::StaleSource);
    r.source_utc=now;
    r.counter_bound_to_sample=false;
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::Unverified);
    r.counter_bound_to_sample=true;
    r.documented_uart_counter=false; // uptime must not masquerade as read seq
    CHECK(t.observe(r,now,180)==PumpReadProofStatus::Unverified);
    // Regression from independent PR review: a counter increment cannot
    // make an hour-old but internally consistent SSE sample fresh.
    PumpReadCounterTracker aged;
    PumpReadCounterMessage old{true,true,true,true,7,42,now-3603,now-3600};
    CHECK(aged.observe(old,now,180)==PumpReadProofStatus::StaleSource);
    ++old.successful_uart_reads;
    CHECK(aged.observe(old,now,180)==PumpReadProofStatus::StaleSource);
    old.source_utc=now-3;
    old.received_utc=now+1;
    CHECK(aged.observe(old,now,180)==PumpReadProofStatus::InvalidClock);
    old.received_utc=now;
    CHECK(aged.observe(old,now,180)==PumpReadProofStatus::Baseline);
    ++old.successful_uart_reads;
    CHECK(aged.observe(old,now,180)==PumpReadProofStatus::NewRead);
    CHECK(aged.observe(old,0,180)==PumpReadProofStatus::InvalidClock);
    // Codex P2 regression: 100 -> 101 -> 100 -> 101 MUST NOT recertify
    // the already handled 101 within the same source boot epoch.
    PumpReadCounterTracker replay;
    PumpReadCounterMessage prev{true,true,true,true,7,100,now-2,now};
    CHECK(replay.observe(prev,now,180)==PumpReadProofStatus::Baseline);
    prev.successful_uart_reads=101;
    CHECK(replay.observe(prev,now,180)==PumpReadProofStatus::NewRead);
    prev.successful_uart_reads=100;
    CHECK(replay.observe(prev,now,180)==PumpReadProofStatus::CounterReset);
    prev.successful_uart_reads=101;
    CHECK(replay.observe(prev,now,180)==PumpReadProofStatus::Replay);
    // Reconnect may NOT erase the highest confirmed counter in that epoch.
    replay.invalidate();
    prev.successful_uart_reads=100;
    CHECK(replay.observe(prev,now,180)==PumpReadProofStatus::CounterReset);
    prev.successful_uart_reads=101;
    CHECK(replay.observe(prev,now,180)==PumpReadProofStatus::Baseline);
    prev.successful_uart_reads=102;
    CHECK(replay.observe(prev,now,180)==PumpReadProofStatus::NewRead);
    // Codex P2: an old boot session must NEVER be reopened by replay.
    PumpReadCounterTracker epochs;
    PumpReadCounterMessage ep{true,true,true,true,7,100,now-20,now};
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::Baseline);
    ep.successful_uart_reads=101;ep.source_utc=now-19;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::NewRead);
    ep.boot_epoch=8;ep.successful_uart_reads=1;ep.source_utc=now-10;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::Baseline);
    ep.boot_epoch=7;ep.successful_uart_reads=100;ep.source_utc=now-20;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::CounterReset);
    ep.successful_uart_reads=101;ep.source_utc=now-19;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::CounterReset);
    // Failed old-epoch replay means next current-epoch sample is baseline.
    ep.boot_epoch=8;ep.successful_uart_reads=2;ep.source_utc=now-9;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::Baseline);
    ep.successful_uart_reads=3;ep.source_utc=now-8;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::NewRead);
    // A nonmonotonic/new random epoch is not proof of a real reboot.
    ep.boot_epoch=6;ep.successful_uart_reads=1;ep.source_utc=now-7;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::CounterReset);
    // An epoch that appears newer but carries an older source timestamp is replay.
    ep.boot_epoch=9;ep.successful_uart_reads=1;ep.source_utc=now-19;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::CounterReset);
    ep.boot_epoch=8;ep.successful_uart_reads=4;ep.source_utc=now-6;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::Baseline);
    ep.successful_uart_reads=5;ep.source_utc=now-5;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::NewRead);
    // A higher counter with an earlier pump-source time is not a fresh UART read.
    ep.successful_uart_reads=6;ep.source_utc=now-30;
    CHECK(epochs.observe(ep,now,180)==PumpReadProofStatus::Replay);
    CHECK(!confirmed_pump_serial_update({true,true,5,5}));
    CHECK(assess_measurement({now,now-181,true},now,180)==MeasurementQuality::StaleSource);
    CHECK(assess_measurement({now,0,false},now,180)==MeasurementQuality::UnverifiedSource);
    std::puts("PASS source read counter, replay/reconnect, reset, UART loss and clocks");
}
