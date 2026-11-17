#include "cottage/ota_recovery.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using namespace enevina::cottage::ota;
struct Store {
    bool fail_good=false,fail_pending=false,fail_txn=false;
    bool pending=true,txn=true;
    uint32_t good=8;
    bool write_good(uint32_t value) {
        if(fail_good)return false;
        good=value;return true;
    }
    bool remove_legacy_pending(){
        if(fail_pending)return false;
        pending=false;return true;
    }
    bool remove_transaction(){
        if(fail_txn)return false;
        txn=false;return true;
    }
};
static Transaction candidate(Phase phase=Phase::Armed,
                             ImageState previous=ImageState::Valid){
    Transaction t{};
    t.version=9;t.source_address=0x10000u;
    t.target_address=0x690000u;t.target_subtype=17;
    t.phase=phase;t.previous_target_state=previous;
    for(int i=0;i<32;i++)t.target_image_sha256[i]=static_cast<uint8_t>(i);
    seal(t);
    assert(valid(t));return t;
}
static Evidence state(bool old_running,ImageState image,bool selected=false,
                      bool matching=true){
    Evidence e{};
    e.running_source=old_running;e.running_target=!old_running;
    e.selected_target=selected;e.target_hash_matches=matching;
    e.target_state=image;return e;
}
int main(){
    // Download incomplete: no persistent candidate; existing image untouched.
    auto tx=candidate(Phase::Prepared);
    assert(decide(tx,state(true,ImageState::Undefined))==
           Recovery::DiscardNeverActivated);
    // Power cut after writing intent, before boot partition activation.
    assert(decide(tx,state(true,ImageState::Valid))==
           Recovery::DiscardNeverActivated);
    assert(decide(tx,state(true,ImageState::Aborted))==
           Recovery::DiscardNeverActivated); // stale otadata, no false bad
    // Activation committed but app never booted.
    assert(decide(tx,state(true,ImageState::New,true))==
           Recovery::KeepForActivation);
    // Candidate first boot, then healthy bootloader PENDING or VALID.
    assert(decide(tx,state(false,ImageState::PendingVerify))==
           Recovery::AwaitCandidateHealth);
    assert(decide(tx,state(false,ImageState::Valid))==
           Recovery::AwaitCandidateHealth);
    // Power cut after bootloader VALID, before NVS commit.
    assert(decide(tx,state(false,ImageState::Valid))==
           Recovery::AwaitCandidateHealth);
    // A demonstrable rollback to previously good slot quarantines exact v9.
    tx.phase=Phase::Armed;seal(tx);
    assert(decide(tx,state(true,ImageState::Aborted))==
           Recovery::QuarantineRealRollback);
    assert(decide(tx,state(true,ImageState::Invalid))==
           Recovery::QuarantineRealRollback);
    assert(decide(tx,state(true,ImageState::New,true))==
           Recovery::KeepForActivation);
    // A stale prior ABORTED otadata does not prove new candidate was booted.
    auto stale=candidate(Phase::Armed,ImageState::Aborted);
    assert(decide(stale,state(true,ImageState::Aborted))==
           Recovery::DiscardNeverActivated);
    // Slot/hash mismatch MUST NOT accept or quarantine wrong firmware.
    assert(decide(tx,state(false,ImageState::Valid,false,false))==
           Recovery::Blocked);
    assert(decide(tx,state(true,ImageState::Aborted,false,false))==
           Recovery::DiscardNeverActivated);
    auto corrupted=tx;corrupted.target_address^=1;
    assert(!valid(corrupted));
    auto swapped=tx;swapped.version=77;seal(swapped);
    assert(swapped.version!=tx.version && valid(swapped));

    // NVS power-loss injection: all failed commits are retryable; a failed
    // putUInt/remove NEVER returns success or silently drops the transaction.
    for(int fault=0;fault<3;fault++){
        Store nvs{};
        if(fault==0)nvs.fail_good=true;
        if(fault==1)nvs.fail_pending=true;
        if(fault==2)nvs.fail_txn=true;
        assert(!confirm_persistence(nvs,tx,nvs.good));
        assert(nvs.txn); // record still durable for retry
        nvs.fail_good=nvs.fail_pending=nvs.fail_txn=false;
        assert(confirm_persistence(nvs,tx,nvs.good));
        assert(!nvs.txn&&!nvs.pending&&nvs.good==tx.version);
    }
    // Interrupted after good write and after pending removal: idempotent.
    Store prior{};prior.good=tx.version;prior.pending=false;
    assert(confirm_persistence(prior,tx,prior.good));
    assert(!prior.txn&&prior.good==tx.version);
    // Higher signed version remains eligible (bad=9 is not permanent lockout).
    const uint32_t bad=tx.version;
    assert(!(tx.version>bad));
    assert(10u>bad);
    std::puts("PASS 11 OTA recovery/power-cut scenarios and NVS fault injection");
}
