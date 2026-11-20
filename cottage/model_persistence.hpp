#pragma once
// Persistent, verifiable reset of the learned building model.
//
// Erasing the NVS blob alone is not enough: the erase, the commit or the
// key lookup can fail, and a reboot before the next successful save would
// then restore the OLD calibration after the owner reset it (Codex P2,
// PR #63). The stored model is therefore bound to a persistent "reset
// epoch": a reset (1) tries to erase the blob and verifies it is gone and
// (2) advances the epoch and verifies it by reading it back. The model
// identity used by save()/load() includes the epoch, so a blob that
// survived a failed erase can never be loaded again once the epoch moved.
// A reset is reported persisted only when the blob is verifiably gone OR
// the epoch verifiably moved; otherwise the caller must say so and retry.
//
// Pure and header-only so the failure paths are host-tested with an
// injected store; the firmware adapter implements the same four calls on
// top of the ESP-IDF NVS.
#include <cstdint>

namespace enevina::cottage {

enum class EpochRead : uint8_t { Ok, Missing, Error };
enum class ModelResetResult : uint8_t { Persisted, PersistFailed };

// Identity bound into the stored blob: site/pump identity AND reset epoch.
inline uint32_t model_store_identity(uint32_t base,uint32_t epoch) {
    uint32_t h=base^0x9E3779B9u;
    for(int i=0;i<4;++i) {h^=(epoch>>(8*i))&0xFFu;h*=16777619u;}
    return h;
}

// Store requirements:
//   EpochRead read_epoch(uint32_t&)   Missing = never reset (epoch 0)
//   bool write_epoch(uint32_t)        set + commit
//   bool erase_model()                erase key + commit; true when the key
//                                     is absent afterwards (absent counts)
//   bool model_present()              true unless the key is VERIFIED absent
template<class Store>
bool load_model_epoch(Store& store,uint32_t& epoch) {
    epoch=0;
    uint32_t v=0;
    switch(store.read_epoch(v)) {
    case EpochRead::Ok:epoch=v;return true;
    case EpochRead::Missing:return true;
    case EpochRead::Error:break;
    }
    return false;   // epoch unknown: do not restore a model that may be stale
}

// `epoch` follows what NVS verifiably holds; `epoch_known` says whether the
// current stored epoch was actually read. Epoch advancement counts as proof
// of a reset ONLY from a KNOWN current epoch: advancing from a guessed value
// could rewrite the epoch the surviving blob is already bound to, and the
// old calibration would load again after a reboot. With an unknown epoch only
// a verified blob removal counts as persisted (Codex P2, PR #63).
template<class Store>
ModelResetResult persist_model_reset(Store& store,uint32_t& epoch,bool& epoch_known) {
    uint32_t current=0;
    switch(store.read_epoch(current)) {
    case EpochRead::Ok:epoch=current;epoch_known=true;break;
    case EpochRead::Missing:epoch=0;epoch_known=true;break;
    case EpochRead::Error:epoch_known=false;break;
    }
    const bool erased=store.erase_model() && !store.model_present();
    bool marked=false;
    const uint32_t next=epoch+1;
    if(epoch_known && store.write_epoch(next)) {
        uint32_t readback=0;
        marked=store.read_epoch(readback)==EpochRead::Ok && readback==next;
    }
    uint32_t stored=0;
    if(store.read_epoch(stored)==EpochRead::Ok) {epoch=stored;epoch_known=true;}
    else if(marked) {epoch=next;epoch_known=true;}
    return (erased||marked)?ModelResetResult::Persisted:ModelResetResult::PersistFailed;
}

inline const char* model_reset_result_name(ModelResetResult r) {
    return r==ModelResetResult::Persisted?"PERSISTED":"PERSIST_FAILED";
}
} // namespace enevina::cottage
