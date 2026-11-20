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
    // Exact ESP-IDF 4.4 (Arduino 2.0.17) numeric values. A raw cast
    // silently shifts every bootloader state and rejects healthy OTA.
    assert(decode_esp_idf_image_state(0u)==ImageState::New);
    assert(decode_esp_idf_image_state(1u)==ImageState::PendingVerify);
    assert(decode_esp_idf_image_state(2u)==ImageState::Valid);
    assert(decode_esp_idf_image_state(3u)==ImageState::Invalid);
    assert(decode_esp_idf_image_state(4u)==ImageState::Aborted);
    assert(decode_esp_idf_image_state(0xffffffffu)==ImageState::Undefined);
    assert(decode_esp_idf_image_state(99u)==ImageState::Undefined);
    // Real IDF PENDING=1 and VALID=2 MUST enter confirmation/retry.
    auto idf_candidate=candidate();
    assert(decide(idf_candidate,
           state(false,decode_esp_idf_image_state(1u)))==
           Recovery::AwaitCandidateHealth);
    assert(decide(idf_candidate,
           state(false,decode_esp_idf_image_state(2u)))==
           Recovery::AwaitCandidateHealth);
    // Download incomplete: no persistent candidate; existing image untouched.
    auto tx=candidate(Phase::Prepared);
    assert(decide(tx,state(true,ImageState::Undefined))==
           Recovery::DiscardNeverActivated);
    // Power cut after writing intent, before boot partition activation.
    assert(decide(tx,state(true,ImageState::Valid))==
           Recovery::DiscardNeverActivated);
    assert(decide(candidate(Phase::Prepared,ImageState::Aborted),
                  state(true,ImageState::Aborted))==
           Recovery::DiscardNeverActivated); // unchanged stale otadata
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
    // A stale prior ABORTED otadata alone is not enough: Prepared
    // preserves the prior state and cannot prove an attempted boot.
    auto stale=candidate(Phase::Prepared,ImageState::Aborted);
    assert(decide(stale,state(true,ImageState::Aborted))==
           Recovery::DiscardNeverActivated);
    // Armed is written only AFTER set_boot_partition has reset the
    // bootloader's state to NEW. A subsequent ABORTED proves rollback.
    stale.phase=Phase::Armed;seal(stale);
    assert(decide(stale,state(true,ImageState::Aborted))==
           Recovery::QuarantineRealRollback);
    // Even without durable Armed write, a changed previous image-state
    // makes the rollback unambiguous.
    auto interrupted=candidate(Phase::Prepared,ImageState::Valid);
    assert(decide(interrupted,state(true,ImageState::Aborted))==
           Recovery::QuarantineRealRollback);
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
    // P2 #62: a crash can happen after good is written but before the
    // transaction is cleared. The history intent must have been attempted
    // while the signed tx is still available and must be retried on reboot.
    for(int fault=0;fault<3;++fault) {
        Store nvs{};
        nvs.fail_good=fault==0;
        nvs.fail_pending=fault==1;
        nvs.fail_txn=fault==2;
        uint32_t hint=0;
        int hint_writes=0;
        bool hint_fail=true; // optional history cannot gate OTA confirmation
        auto record=[&](uint32_t version) {
            assert(nvs.good==version && nvs.txn);
            ++hint_writes;
            if(!hint_fail) hint=version;
        };
        assert(!confirm_persistence(nvs,tx,nvs.good,record));
        assert(nvs.txn);
        if(fault==0) assert(hint_writes==0); // bad NVS good cannot create hint
        else assert(hint_writes==1); // GOOD is durable before hint
        // Simulate reboot. The old good is now the new version after any
        // partial write; the persisted transaction survives for retry.
        nvs.fail_good=nvs.fail_pending=nvs.fail_txn=false;
        hint_fail=false;
        assert(confirm_persistence(nvs,tx,nvs.good,record));
        assert(hint==tx.version && hint_writes==(fault==0?1:2));
        assert(!nvs.txn && !nvs.pending && nvs.good==tx.version);
    }
    // The optional history callback failing must NOT block healthy signed
    // image confirmation. It is retried if a transaction remains.
    Store no_history{};
    bool attempted=false;
    assert(confirm_persistence(no_history,tx,no_history.good,
        [&](uint32_t version) {
            assert(version==tx.version && no_history.txn && no_history.good==version);
            attempted=true;
        }));
    assert(attempted && !no_history.txn && no_history.good==tx.version);
    // Interrupted after good write and after pending removal: idempotent.
    Store prior{};prior.good=tx.version;prior.pending=false;
    assert(confirm_persistence(prior,tx,prior.good));
    assert(!prior.txn&&prior.good==tx.version);
    // NVS-open failure is fail-closed and the intact transaction is
    // retryable when storage becomes available again.
    Store unavailable{};
    bool can_open_nvs=false;
    auto confirm_if_open=[&]() {
        return can_open_nvs && confirm_persistence(unavailable,tx,unavailable.good);
    };
    assert(!confirm_if_open() && unavailable.txn);
    can_open_nvs=true;
    assert(confirm_if_open() && !unavailable.txn);
    // Same defective signed version stays quarantined, but a higher
    // verified version can still repair the controller after recovery.
    const uint32_t bad=tx.version;
    assert(!signed_version_eligible(tx.version,8,bad,false));
    assert(signed_version_eligible(10,8,bad,false));
    assert(!signed_version_eligible(10,8,bad,true));
    std::puts("PASS 11 OTA recovery/power-cut scenarios and NVS fault injection");
}
