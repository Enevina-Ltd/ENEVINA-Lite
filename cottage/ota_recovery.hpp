#pragma once
// Portable C2.4 OTA recovery policy. No ESP/NVS dependencies; fail closed.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enevina::cottage::ota {
constexpr uint32_t kTxnMagic = 0x4F544132u; // OTA2
constexpr uint32_t kTxnFormat = 1;
enum class Phase : uint8_t { Prepared = 1, Armed = 2 };
enum class ImageState : uint8_t {
    Undefined = 0, New = 1, PendingVerify = 2, Valid = 3,
    Invalid = 4, Aborted = 5
};
// ESP-IDF's esp_ota_img_states_t values are NOT our portable enum:
// NEW=0, PENDING=1, VALID=2, INVALID=3, ABORTED=4,
// UNDEFINED=0xffffffff. Never use static_cast<ImageState>(raw).
inline ImageState decode_esp_idf_image_state(uint32_t raw) {
    switch(raw) {
    case 0u:return ImageState::New;
    case 1u:return ImageState::PendingVerify;
    case 2u:return ImageState::Valid;
    case 3u:return ImageState::Invalid;
    case 4u:return ImageState::Aborted;
    default:return ImageState::Undefined;
    }
}
struct Transaction {
    uint32_t magic = kTxnMagic;
    uint32_t format = kTxnFormat;
    uint32_t version = 0;
    uint32_t source_address = 0;
    uint32_t target_address = 0;
    uint8_t target_subtype = 0;
    Phase phase = Phase::Prepared;
    ImageState previous_target_state = ImageState::Undefined;
    uint8_t reserved = 0;
    uint8_t target_image_sha256[32]{};
    uint32_t checksum = 0;
};
// One NVS putBytes() writes a fixed-layout, checksummed record.
static_assert(sizeof(Transaction)==60,"OTA transaction layout changed");
inline uint32_t checksum(Transaction t) {
    t.checksum = 0;
    uint32_t v = 2166136261u;
    const auto* p = reinterpret_cast<const uint8_t*>(&t);
    for(size_t i=0;i<sizeof(t);++i) {v^=p[i];v*=16777619u;}
    return v;
}
inline void seal(Transaction& t) { t.checksum=checksum(t); }
inline bool valid(const Transaction& t) {
    return t.magic==kTxnMagic && t.format==kTxnFormat && t.version!=0 &&
           t.target_address!=t.source_address && t.target_address!=0 &&
           t.phase>=Phase::Prepared && t.phase<=Phase::Armed &&
           t.checksum==checksum(t);
}
inline bool signed_version_eligible(uint32_t version,uint32_t good,
                                    uint32_t bad,bool transaction_pending) {
    return version!=0 && version>good && version>bad &&
           !transaction_pending;
}
// A durable high-water mark distinguishes an already attempted release
// from a new one, even after power loss. Explicit requests may retry a
// transfer, but must still pass the separate signed-version/rollback gate.
inline bool image_attempt_allowed(uint32_t version,uint32_t attempted,bool explicit_request) {
    return version!=0 && (version>attempted || explicit_request);
}
struct Evidence {
    bool running_source=false, running_target=false;
    bool selected_target=false, target_hash_matches=false;
    ImageState target_state=ImageState::Undefined;
};
enum class Recovery {
    Blocked, AwaitCandidateHealth, KeepForActivation,
    DiscardNeverActivated, QuarantineRealRollback
};
// NVS writes are not atomic with bootloader VALID. Keep the transaction
// until every durable step succeeds; another call (even after reboot)
// can safely complete any partially applied good/cleanup sequence.
template<class Store,class OnDurableGood>
bool confirm_persistence(Store& store,const Transaction& tx,uint32_t current_good,
                         OnDurableGood on_durable_good) {
    if(!valid(tx))return false;
    if(current_good<tx.version && !store.write_good(tx.version))
        return false;
    // The signed candidate's good version has been committed (or was already
    // committed before a power cut). Give optional history a chance to save
    // its intent WHILE txv2 is still durable. Do not make a full event/NVS
    // history partition block successful OTA confirmation.
    on_durable_good(tx.version);
    if(!store.remove_legacy_pending())return false;
    if(!store.remove_transaction())return false;
    return true;
}
template<class Store>
bool confirm_persistence(Store& store,const Transaction& tx,uint32_t current_good) {
    return confirm_persistence(store,tx,current_good,[](uint32_t){});
}
inline Recovery decide(const Transaction& tx,const Evidence& e) {
    if(!valid(tx))return Recovery::Blocked;
    if(e.running_target) {
        if(!e.target_hash_matches)return Recovery::Blocked;
        if(e.target_state==ImageState::PendingVerify ||
           e.target_state==ImageState::Valid)
            return Recovery::AwaitCandidateHealth;
        return Recovery::Blocked;
    }
    if(!e.running_source)return Recovery::Blocked;
    // Boot selection was committed but no candidate boot has occurred.
    if(e.selected_target && e.target_hash_matches &&
       e.target_state==ImageState::New)
        return Recovery::KeepForActivation;
    // A failed attempt is evidenced by bootloader ABORTED/INVALID in
    // the exact candidate slot with matching image identity, after a
    // durable Armed record. Pre-existing ABORTED/INVALID is ambiguous.
    const bool failed=e.target_state==ImageState::Aborted ||
                      e.target_state==ImageState::Invalid;
    const bool previously_failed=
        tx.previous_target_state==ImageState::Aborted ||
        tx.previous_target_state==ImageState::Invalid;
    // An Armed record was stored only AFTER successful boot selection,
    // which replaces even a previously ABORTED OTA-state with NEW.
    // A Prepared record also proves a rollback if the old state was not
    // already failed; the tiny pre-Armed power-cut window is then covered.
    if(e.target_hash_matches && failed &&
       (tx.phase==Phase::Armed || !previously_failed))
        return Recovery::QuarantineRealRollback;
    // A Prepared record may predate esp_ota_set_boot_partition. A
    // power failure here is NEVER evidence of a defective release.
    if(!e.selected_target)
        return Recovery::DiscardNeverActivated;
    return Recovery::Blocked;
}
}
