#include "cottage/cn105_source_proof.hpp"
#include <cstdio>
#include <cstdlib>
using namespace enevina;
using namespace enevina::cottage;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);std::exit(1); } } while(false)
int main() {
    constexpr int64_t now=1800000000;
    CN105SourceProofVerifier proof;
    CN105Telemetry accepted{};
    CN105ProvenPoll p{1,77,100,now-5,now-2,now,true,true,19.0,21.0,35,ObservedPumpMode::Heat};
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Baseline);
    CHECK(!accepted.received); // a reconnect's replayed first snapshot isn't new
    p.completed_poll_sequence=101;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::NewRead);
    CHECK(valid_cn105(accepted,now));
    CHECK(accepted.evidence.source_utc==now-5);
    CHECK(accepted.evidence.received_utc==now);
    // Identical temperature from a genuinely separate UART poll is fresh.
    ++p.completed_poll_sequence;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::NewRead);
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Replay);
    proof.reconnect();
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Baseline);
    ++p.completed_poll_sequence;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::NewRead);
    p.uart_connected=false;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Unverified);
    p.uart_connected=true;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Baseline);
    p.completed_poll_sequence=1; // reset without reboot-epoch change
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::CounterReset);
    ++p.completed_poll_sequence;
    // A same-epoch reset cannot re-certify old samples by climbing slowly.
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::CounterReset);
    ++p.boot_epoch; // audited persistent monotonic generation; NOT random ID
    p.oldest_uart_read_utc=now-4; // a reboot must carry a strictly newer source read
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Baseline);
    ++p.completed_poll_sequence;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::NewRead);
    // A delayed previous-session SSE snapshot is never a fresh UART poll.
    p.boot_epoch=77;p.completed_poll_sequence=101;p.oldest_uart_read_utc=now-5;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::CounterReset);
    p.boot_epoch=78;p.completed_poll_sequence=4;p.oldest_uart_read_utc=now-3;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Baseline);
    ++p.completed_poll_sequence;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::NewRead);
    ++p.completed_poll_sequence;
    p.latest_uart_read_utc=now+1;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::InvalidClock);
    p.latest_uart_read_utc=now-2;
    p.oldest_uart_read_utc=now-190;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::StaleSource);
    p.oldest_uart_read_utc=now-5;
    p.complete_decoded_poll=false;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Unverified);
    p.complete_decoded_poll=true;
    p.schema=2;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Unverified);
    p.schema=1;
    p.completed_poll_sequence=0;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Unverified);
    p.completed_poll_sequence=50;
    p.mode=ObservedPumpMode::Unknown;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Unverified);
    p.mode=ObservedPumpMode::Heat;
    p.indoor_c=200;
    CHECK(proof.accept(p,accepted)==PumpReadProofStatus::Unverified);
    std::puts("PASS CN105 atomic UART 0x02/0x03/0x06 source proof and fail-closed cases");
}
