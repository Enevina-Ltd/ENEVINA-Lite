// Reset of the learned model must survive NVS failures and reboots.
#include "cottage/building_identification.hpp"
#include "cottage/model_persistence.hpp"
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
    bool fail_presence_check=false;
    EpochRead read_epoch(uint32_t& out) {
        if(fail_epoch_read) return EpochRead::Error;
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
    CHECK(persist_model_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(epoch==1 && nvs.epoch==1 && !nvs.has_blob);
    CHECK(!reboot_restores(nvs));
}
void test_erase_fails_epoch_still_blocks_old_model() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.fail_erase=true;
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(persist_model_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(nvs.has_blob);                    // the stale blob is still there ...
    CHECK(!reboot_restores(nvs));           // ... but can never be loaded
}
void test_silent_erase_is_verified_not_trusted() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.silent_erase_noop=true;
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(persist_model_reset(nvs,epoch)==ModelResetResult::Persisted);   // via epoch
    CHECK(!reboot_restores(nvs));
    // Silent erase AND a failing epoch: nothing persisted -> reported failed.
    FakeNvs bad;bad.save(learned(),base_identity);
    bad.silent_erase_noop=true;bad.fail_epoch_write=true;
    epoch=0;CHECK(load_model_epoch(bad,epoch));
    CHECK(persist_model_reset(bad,epoch)==ModelResetResult::PersistFailed);
    CHECK(epoch==0);
}
void test_epoch_write_fails_erase_removes_blob() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.fail_epoch_write=true;
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(persist_model_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(!nvs.has_blob && epoch==0);
    CHECK(!reboot_restores(nvs));
}
void test_all_operations_fail_is_reported_and_retry_succeeds() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.fail_erase=true;nvs.fail_epoch_write=true;
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(persist_model_reset(nvs,epoch)==ModelResetResult::PersistFailed);
    // The old calibration WOULD come back after a reboot here: the caller
    // must therefore not tell the owner the reset is permanent.
    CHECK(reboot_restores(nvs));
    // Storage recovers; the retry persists and the old model is gone.
    nvs.fail_erase=false;nvs.fail_epoch_write=false;
    CHECK(persist_model_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(!reboot_restores(nvs));
}
void test_unreadable_epoch_blocks_restore_and_unverifiable_presence_is_failure() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    nvs.fail_epoch_read=true;
    CHECK(!reboot_restores(nvs));           // epoch unknown: do not trust the blob
    FakeNvs p;p.save(learned(),base_identity);
    p.fail_presence_check=true;p.fail_epoch_write=true;
    uint32_t epoch=0;CHECK(load_model_epoch(p,epoch));
    CHECK(persist_model_reset(p,epoch)==ModelResetResult::PersistFailed);
}
void test_new_learning_after_reset_persists_and_second_reset_works() {
    FakeNvs nvs;nvs.save(learned(),base_identity);
    uint32_t epoch=0;CHECK(load_model_epoch(nvs,epoch));
    CHECK(persist_model_reset(nvs,epoch)==ModelResetResult::Persisted);
    nvs.save(learned(),base_identity);      // saved under epoch 1
    CHECK(nvs.epoch==1 && reboot_restores(nvs));
    CHECK(persist_model_reset(nvs,epoch)==ModelResetResult::Persisted);
    CHECK(epoch==2 && !reboot_restores(nvs));
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
    test_identity_binds_epoch_and_site();
    if(failures) {std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("cottage_model_persistence_tests: all passed\n");
    return 0;
}
