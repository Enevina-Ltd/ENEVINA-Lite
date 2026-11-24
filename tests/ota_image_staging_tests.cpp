#include "cottage/ota_image_staging.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace enevina::cottage::ota;
struct Source {
    std::vector<uint8_t> bytes{1,2,3,4,5,6,7,8,9};
    size_t offset=0, observed=0;
    unsigned checks=0, waits=0;
    unsigned fail_ready_at=0;
    int fail_read_at=-1;
    bool stalled=false, overread=false, hash_failed=false, complete_ok=true;
    StageResult digest=StageResult::Complete;
    bool ready(){return ++checks!=fail_ready_at;}
    int read(uint8_t* dest,size_t max){
        if(stalled)return 0;
        if(overread)return static_cast<int>(max+1);
        if(fail_read_at>=0 && offset>=static_cast<size_t>(fail_read_at))return -1;
        size_t n=std::min({max,size_t(3),bytes.size()-offset});
        std::memcpy(dest,bytes.data()+offset,n);
        offset+=n;
        return static_cast<int>(n);
    }
    bool hash(const uint8_t*,size_t){return !hash_failed;}
    bool complete(){return complete_ok && offset==bytes.size();}
    StageResult verify(){return digest;}
    void progress(size_t done){assert(done>observed);observed=done;}
    void wait(){++waits;}
};
int main(){
    std::array<uint8_t,11> storage{};
    uint8_t scratch[4]{};
    auto run=[&](Source& src,size_t n=9,size_t cap=9){
        storage.fill(0xa5);
        auto result=stage_image(storage.data()+1,cap,n,scratch,sizeof(scratch),src);
        assert(storage.front()==0xa5 && storage.back()==0xa5);
        return result;
    };
    Source good; assert(run(good)==StageResult::Complete);
    assert(std::equal(good.bytes.begin(),good.bytes.end(),storage.begin()+1));
    assert(good.observed==9);
    Source link;link.fail_ready_at=3;assert(run(link)==StageResult::NotReady);
    Source last;last.fail_ready_at=6;assert(run(last)==StageResult::NotReady);
    assert(last.observed==6); // even final read is rejected after a gate loss
    Source read;read.fail_read_at=3;assert(run(read)==StageResult::ReadError);
    Source stall;stall.stalled=true;assert(run(stall)==StageResult::Stalled && stall.waits==25);
    Source over;over.overread=true;assert(run(over)==StageResult::WrongLength);
    Source length;length.complete_ok=false;assert(run(length)==StageResult::WrongLength);
    Source shortbody;shortbody.bytes.resize(5);assert(run(shortbody)==StageResult::Stalled);
    Source hash;hash.hash_failed=true;assert(run(hash)==StageResult::HashError);
    Source corrupt;corrupt.digest=StageResult::DigestMismatch;assert(run(corrupt)==StageResult::DigestMismatch);
    Source capacity;assert(run(capacity,9,8)==StageResult::WrongLength && capacity.checks==0);
    Source zero;assert(run(zero,0)==StageResult::WrongLength);
    puts("PASS: complete staging, gate loss, partial/truncated stream, stall, overrun, bad digest, capacity and hash failure");
}
