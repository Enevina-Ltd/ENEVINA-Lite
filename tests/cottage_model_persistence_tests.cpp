// Reset of the learned model must survive NVS failures and reboots.
#include "cottage/building_identification.hpp"
#include "cottage/model_persistence.hpp"
#include "cottage/learning_observation.hpp"
#include "cottage/model_checkpoint.hpp"
#include "cottage/maintenance_cycle.hpp"
#include "tests/cottage_closed_loop.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace enevina::cottage;

namespace {
int failures=0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); ++failures; } } while(0)

// In-memory NVS with per-operation fault injection.
struct FakeNvs {
    bool has_blob=false;
    std::vector<uint8_t> blob;
    bool has_epoch=false;
    uint32_t epoch=0;
    bool fail_erase=false;          // erase reports an error, key stays
    bool silent_erase_noop=false;   // erase reports OK but the key stays
    bool fail_epoch_write=false;    // write reports an error, value unchanged
    bool fail_epoch_read=false;
    int fail_epoch_reads_left=0;    // next N reads fail, then it recovers
    bool fail_presence_check=false;
    EpochRead read_epoch(uint32_t& out) {
        if(fail_epoch_read) return EpochRead::Error;
        if(fail_epoch_reads_left>0) {--fail_epoch_reads_left;return EpochRead::Error;}
        if(!has_epoch) return EpochRead::Missing;
        out=epoch;return EpochRead::Ok;
    }
    bool write_epoch(uint32_t v) {
        if(fail_epoch_write) return false;
        epoch=v;has_epoch=true;return true;
    }
    bool erase_model() {
        if(fail_erase) return false;
        if(!silent_erase_noop) {has_blob=false;blob.clear();}
        return true;
    }
    bool model_present() { return fail_presence_check?true:has_blob; }
    void save(const BuildingIdentifier& id,uint32_t base) {
        uint8_t buf[BuildingIdentifier::blob_capacity];
        size_t used=0;
        CHECK(id.save(model_store_identity(base,epoch),buf,sizeof(buf),used));
        blob.assign(buf,buf+used);has_blob=true;
    }
};
constexpr uint32_t base_identity=0xC0FFEE11u;
ModelResetResult do_reset(FakeNvs& nvs,uint32_t& epoch) {
    bool known=false;
    return persist_model_reset(nvs,epoch,known);
}

BuildingIdentifier learned() {
    BuildingIdentifier id;
    closed_loop::Rng rng{7};
    int64_t utc=1768168800-10*86400;
    for(int i=0;i<300;++i,utc+=900) {
        SlotObservation o{};
        o.slot_utc=utc;o.indoor_valid=true;o.outdoor_valid=true;o.outdoor_c=-3+rng.normal();
        o.heat_valid=true;o.heat_requested=(i/4)%2==0;o.in_model_domain=true;
        o.indoor_start_c=12;o.indoor_end_c=12.05+(o.heat_requested?0.1:-0.1);
        id.add(o);
    }
    return id;
}
// A reboot: read the epoch, then try to restore with the epoch-bound identity.
bool reboot_restores(FakeNvs& nvs) {
    uint32_t epoch=0;
    if(!load_model_epoch(nvs,epoch)) return false;
    if(!nvs.has_blob) return false;
    BuildingIdentifier id;
    return id.load(model_store_identity(base_identity,epoch),nvs.blob.data(),nvs.blob.size());
}

void test_baseline_restores_without_reset() {
    FakeNvs nvs;const auto id=learned();
    CHECK(id.report(0).samples>50);
    nvs.save(id,base_identity);
    CHECK(reboot_restores(nvs));
}
void test_clean_reset_does_not_come_back() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(do_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(epoch==1 && nvs.epoch==1 && !nvs.has_blob);
    CHECK(!reboot_restores(nvs));
}
void test_erase_fails_epoch_still_blocks_old_model() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.fail_erase=true;
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(do_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(nvs.has_blob);                    // the stale blob is still there ...
    CHECK(!reboot_restores(nvs));           // ... but can never be loaded
}
void test_silent_erase_is_verified_not_trusted() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.silent_erase_noop=true;
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(do_reset(nvs,epoch)==ModelResetResult::Persisted);   // via epoch
    CHECK(!reboot_restores(nvs));
    // Silent erase AND a failing epoch: nothing persisted -> reported failed.
    FakeNvs bad;bad.save(learned(),base_identity);
    bad.silent_erase_noop=true;bad.fail_epoch_write=true;
    epoch=0;CHECK(load_model_epoch(bad,epoch));
    CHECK(do_reset(bad,epoch)==ModelResetResult::PersistFailed);
    CHECK(epoch==0);
}
void test_epoch_write_fails_erase_removes_blob() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.fail_epoch_write=true;
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(do_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(!nvs.has_blob && epoch==0);
    CHECK(!reboot_restores(nvs));
}
void test_all_operations_fail_is_reported_and_retry_succeeds() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.fail_erase=true;nvs.fail_epoch_write=true;
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(do_reset(nvs,epoch)==ModelResetResult::PersistFailed);
    // The old calibration WOULD come back after a reboot here: the caller
    // must therefore not tell the owner the reset is permanent.
    CHECK(reboot_restores(nvs));
    // Storage recovers; the retry persists and the old model is gone.
    nvs.fail_erase=false;nvs.fail_epoch_write=false;
    CHECK(do_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(!reboot_restores(nvs));
}
void test_unreadable_epoch_blocks_restore_and_unverifiable_presence_is_failure() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.fail_epoch_read=true;
    CHECK(!reboot_restores(nvs));           // epoch unknown: do not trust the blob
    FakeNvs p;p.save(learned(),base_identity);
    p.fail_presence_check=true;p.fail_epoch_write=true;
    uint32_t epoch=0;CHECK(load_model_epoch(p,epoch));
    CHECK(do_reset(p,epoch)==ModelResetResult::PersistFailed);
}
void test_new_learning_after_reset_persists_and_second_reset_works() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(do_reset(nvs,epoch)==ModelResetResult::Persisted);
    nvs.save(learned(),base_identity);      // saved under epoch 1
    CHECK(nvs.epoch==1 && reboot_restores(nvs));
    CHECK(do_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(epoch==2 && !reboot_restores(nvs));
}
void test_unknown_epoch_never_counts_as_a_reset() {
    // Codex P2: stored epoch is already 1 (blob bound to epoch 1). Both the
    // boot read and the reset's own read fail and the erase fails. Writing
    // "epoch 1" would be a no-op that still looks like a reset.
    FakeNvs nvs;nvs.has_epoch=true;nvs.epoch=1;nvs.save(learned(),base_identity);
    nvs.fail_epoch_reads_left=2;
    uint32_t epoch=0;
    CHECK(!load_model_epoch(nvs,epoch));            // boot read fails: unknown
    nvs.fail_erase=true;
    bool known=false;
    CHECK(persist_model_reset(nvs,epoch,known)==ModelResetResult::PersistFailed);
    CHECK(nvs.epoch==1);                            // the epoch was NOT rewritten from a guess
    CHECK(reboot_restores(nvs));                    // old model still loads: honestly "failed"
    // Reads recover: the retry advances from the KNOWN epoch and really resets.
    bool known2=false;
    CHECK(do_reset(nvs,epoch)==ModelResetResult::Persisted);
    (void)known2;
    CHECK(nvs.epoch==2 && epoch==2);
    CHECK(!reboot_restores(nvs));
    // Unknown epoch but the blob is verifiably erased: that alone is persisted.
    FakeNvs e2;e2.has_epoch=true;e2.epoch=3;e2.save(learned(),base_identity);
    e2.fail_epoch_read=true;
    uint32_t ep=0;bool k=false;
    CHECK(persist_model_reset(e2,ep,k)==ModelResetResult::Persisted);
    CHECK(!e2.has_blob && e2.epoch==3 && !k);       // nothing guessed, epoch still unknown
}
struct FakeCheckpointFile : FileBackend {
    std::vector<uint8_t> content,pending;
    bool present=false,writing=false;
    bool begin_fail=false,write_fail=false,commit_fail=false,read_fail=false,corrupt_commit=false;
    bool exists()override{return present;}
    size_t size()override{return content.size();}
    bool read(size_t offset,void* dst,size_t n)override{
        if(read_fail || offset>content.size() || n>content.size()-offset)return false;
        std::memcpy(dst,content.data()+offset,n);return true;
    }
    bool append(const void*,size_t)override{return false;}
    bool replace_begin()override{
        if(begin_fail)return false;
        pending.clear();writing=true;return true;
    }
    bool replace_write(const void* data,size_t n)override{
        if(write_fail || !writing)return false;
        auto p=static_cast<const uint8_t*>(data);pending.insert(pending.end(),p,p+n);return true;
    }
    bool replace_commit()override{
        if(commit_fail || !writing)return false;
        content=pending;present=true;writing=false;
        if(corrupt_commit && !content.empty())content[0]^=1;
        return true;
    }
    void replace_abort()override{pending.clear();writing=false;}
};
void test_atomic_model_checkpoint() {
    FakeCheckpointFile file;
    uint8_t buf[BuildingIdentifier::blob_capacity],out[BuildingIdentifier::blob_capacity];
    size_t used=0,n=0;const auto original=learned();
    CHECK(original.save(model_store_identity(base_identity,0),buf,sizeof(buf),used));
    CHECK(read_model_checkpoint(file,out,sizeof(out),n)==CheckpointRead::Missing && n==0);
    CHECK(write_model_checkpoint(file,buf,used));
    CHECK(read_model_checkpoint(file,out,sizeof(out),n)==CheckpointRead::Read && n==used);
    BuildingIdentifier restored;
    CHECK(restored.load(model_store_identity(base_identity,0),out,n));
    CHECK(restored.report(0).samples==original.report(0).samples);
    CHECK(!restored.load(model_store_identity(base_identity,1),out,n)); // reset epoch changed
    CHECK(!restored.load(model_store_identity(base_identity+1,0),out,n)); // changed room/pump
    const auto baseline=file.content;
    for(int fail=0;fail<3;++fail) {
        file.begin_fail=fail==0;file.write_fail=fail==1;file.commit_fail=fail==2;
        CHECK(!write_model_checkpoint(file,buf,used));
        CHECK(file.content==baseline && !file.writing);
    }
    file.begin_fail=file.write_fail=file.commit_fail=false;
    file.read_fail=true;
    CHECK(read_model_checkpoint(file,out,sizeof(out),n)==CheckpointRead::Error && n==0);
    CHECK(!write_model_checkpoint(file,buf,used)); // successful commit is not enough
    file.read_fail=false;file.corrupt_commit=true;
    CHECK(!write_model_checkpoint(file,buf,used));
    CHECK(read_model_checkpoint(file,out,sizeof(out),n)==CheckpointRead::Read);
    CHECK(!restored.load(model_store_identity(base_identity,0),out,n));
    CHECK(restored.report(0).samples==original.report(0).samples);
    file.content.resize(sizeof(out)+1);
    CHECK(read_model_checkpoint(file,out,sizeof(out),n)==CheckpointRead::Error);
}
void test_atomic_maintenance_checkpoint() {
    FakeCheckpointFile file;
    MaintenanceCycleLearner learner,restored;
    MaintenanceCycleSample sample{};
    sample.valid=true;sample.utc=1800000000;sample.meter_id=3;
    sample.room_c=12;sample.outdoor_c=-2;sample.total_wh=1000;sample.base_w=167;
    MaintenanceCycleKey key{model_store_identity(base_identity,0),3,5,12,0,0};
    learner.observe(true,3,1,1,12,sample);
    MaintenanceCycleImage image{},out{};
    CHECK(learner.save(key,image));
    CHECK(write_model_checkpoint(file,reinterpret_cast<const uint8_t*>(&image),sizeof(image)));
    const auto committed=file.content;
    sample.utc+=60;sample.base_w=800;sample.total_wh+=15;
    learner.observe(true,3,1,1,12,sample);
    CHECK(learner.checkpoint_dirty() && learner.save(key,image));
    for(int fail=0;fail<3;++fail) {
        file.begin_fail=fail==0;file.write_fail=fail==1;file.commit_fail=fail==2;
        CHECK(!write_model_checkpoint(file,reinterpret_cast<const uint8_t*>(&image),sizeof(image)));
        CHECK(file.content==committed && !file.writing);
    }
    file.begin_fail=file.write_fail=file.commit_fail=false;
    size_t used=0;
    CHECK(read_model_checkpoint(file,reinterpret_cast<uint8_t*>(&out),sizeof(out),used)==CheckpointRead::Read);
    CHECK(used==sizeof(out));
    CHECK(!restored.restore(key,2,2,sample,out)); // old baseline not reusable
    sample.base_w=167;
    CHECK(restored.restore(key,2,2,sample,out));
    auto reset_key=key;reset_key.identity=model_store_identity(base_identity,1);
    CHECK(!restored.restore(reset_key,2,2,sample,out));
    file.corrupt_commit=true;
    CHECK(!write_model_checkpoint(file,reinterpret_cast<const uint8_t*>(&image),sizeof(image)));
    CHECK(read_model_checkpoint(file,reinterpret_cast<uint8_t*>(&out),sizeof(out),used)==CheckpointRead::Read);
    CHECK(!restored.restore(key,2,2,sample,out)); // CRC/magic
}
void test_learning_interval_and_checkpoint_policy() {
    const int64_t t=1800000000;
    CHECK(learning_room_boundary(t,t-15,t+1));
    CHECK(!learning_room_boundary(t,t-120,t));
    CHECK(!learning_room_boundary(t,t+5,t));
    CHECK(!learning_room_boundary(t,t+61,t+62));
    CHECK(learning_interval_span(t,t+900,1000,901000));
    CHECK(learning_interval_span(t,t+859,1000,860000));
    CHECK(!learning_interval_span(t,t+900,1000,871000)); // clock step
    CHECK(!learning_interval_span(t,t+1000,1000,1001000));
    CHECK(!learning_interval_span(t,t-1,1000,0));
    LearningCheckpoint c;
    c.reset(false);
    CHECK(!c.due(0,true));CHECK(!c.due(0,true));
    CHECK(c.due(1,true)); // first useful observation
    for(int i=0;i<3;++i)CHECK(!c.due(2,true)); // even when write failed
    CHECK(c.due(5,true));
    for(int i=0;i<5;++i)CHECK(!c.due(5,false)); // unchanged model
    CHECK(c.due(6,true));
    c.reset(true); // a restored model is not immediately rewritten
    for(int i=0;i<3;++i)CHECK(!c.due(8,true));
    CHECK(c.due(9,true));
}

void test_identity_binds_epoch_and_site() {
    CHECK(model_store_identity(1,0)!=model_store_identity(1,1));
    CHECK(model_store_identity(1,5)!=model_store_identity(2,5));
}
} // namespace

int main() {
    test_baseline_restores_without_reset();
    test_clean_reset_does_not_come_back();
    test_erase_fails_epoch_still_blocks_old_model();
    test_silent_erase_is_verified_not_trusted();
    test_epoch_write_fails_erase_removes_blob();
    test_all_operations_fail_is_reported_and_retry_succeeds();
    test_unreadable_epoch_blocks_restore_and_unverifiable_presence_is_failure();
    test_new_learning_after_reset_persists_and_second_reset_works();
    test_unknown_epoch_never_counts_as_a_reset();
    test_identity_binds_epoch_and_site();
    test_learning_interval_and_checkpoint_policy();
    test_atomic_model_checkpoint();
    test_atomic_maintenance_checkpoint();
    if(failures) {std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("cottage_model_persistence_tests: all passed\n");
    return 0;
}
