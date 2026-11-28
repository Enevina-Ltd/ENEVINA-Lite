#include "cottage/ota_image_staging.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
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
static unsigned allocations=0,live=0,fail_at=0;
static void* allocate(size_t n){
    if(++allocations==fail_at)return nullptr;
    auto* p=std::malloc(n);if(p)++live;return p;
}
static void release(void* p){if(p){--live;std::free(p);}}
int main(){
    {
        SegmentedImage<4,3> image(allocate,release);
        const uint8_t bytes[]={0,1,2,3,4,5,6,7,8};
        uint8_t out[9]{};
        assert(image.reserve(9)&&live==3);
        assert(image.write(0,bytes,3)); // interrupted in the middle of a block
        assert(image.write(3,bytes+3,6)); // resume across two block boundaries
        assert(image.read(0,out,9)&&std::memcmp(out,bytes,9)==0);
        assert(!image.write(8,bytes,2)&&!image.read(8,out,2));
        size_t span=9;assert(image.span(3,span)&&span==1);
        image.reset();assert(live==0);
        allocations=0;fail_at=2;
        assert(!image.reserve(9)&&live==0);
        assert(!image.reserve(13)&&live==0);
    }
    assert(live==0);
    // A durable prefix is bound to exact authenticated version, size and
    // SHA-256. A torn payload or a mismatched offset never advances it.
    uint8_t digest[32]{};
    digest[0]=17;
    auto identity=spool_identity(2026112707,spool_block_bytes+111,digest);
    assert(spool_valid(identity) && spool_matches(identity,identity));
    assert(spool_retry_allowed(identity.version,identity,identity));
    assert(!spool_retry_allowed(identity.version+1,identity,identity));
    auto different=spool_identity(identity.version+1,identity.size,digest);
    assert(!spool_matches(identity,different));
    digest[0]^=1;
    different=spool_identity(identity.version,identity.size,digest);
    assert(!spool_retry_allowed(identity.version,identity,different));
    different=identity;different.size++;
    assert(!spool_valid(different));
    assert(spool_payload(identity,0)==spool_block_bytes);
    assert(spool_payload(identity,spool_block_bytes)==111);
    assert(!spool_payload(identity,spool_block_bytes+1));
    assert(!spool_payload(identity,2*spool_block_bytes));
    assert(spool_position(0)==sizeof(SpoolIdentity));
    assert(spool_position(spool_block_bytes)==sizeof(SpoolIdentity)+sizeof(SpoolBlock)+spool_block_bytes);
    std::vector<uint8_t> data(spool_block_bytes,42);
    SpoolBlock block{0,spool_block_bytes,spool_crc(data.data(),data.size())};
    assert(spool_block_matches(identity,block,0));
    assert(!spool_block_matches(identity,block,spool_block_bytes));
    uint32_t crc=spool_crc_update(0xffffffffu,data.data(),8192);
    crc=spool_crc_update(crc,data.data()+8192,data.size()-8192);
    assert(~crc==block.crc);
    data[17]^=1;
    assert(spool_crc(data.data(),data.size())!=block.crc);
    block.size--;
    assert(!spool_block_matches(identity,block,0));
    const uint8_t check[]={'1','2','3','4','5','6','7','8','9'};
    assert(spool_crc(check,sizeof(check))==0xcbf43926u);
    std::array<uint8_t,11> storage{};
    uint8_t scratch[4]{};
    auto run=[&](Source& src,size_t n=9,size_t cap=9){
        storage.fill(0xa5);
        auto result=stage_image(storage.data()+1,cap,n,scratch,sizeof(scratch),src);
        assert(storage.front()==0xa5 && storage.back()==0xa5);
        return result;
    };
    const char* tag="\"abc\"";
    const char* range="bytes 3-8/9";
    assert(range_response_matches(206,false,tag,tag,range,range,6,6));
    assert(!range_response_matches(200,false,tag,tag,range,range,6,6));
    assert(!range_response_matches(206,true,tag,tag,range,range,6,6));
    assert(!range_response_matches(206,false,"wrong",tag,range,range,6,6));
    assert(!range_response_matches(206,false,tag,tag,"bytes 0-5/9",range,6,6));
    assert(!range_response_matches(206,false,tag,tag,range,range,5,6));
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
