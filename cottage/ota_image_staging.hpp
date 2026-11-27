#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enevina::cottage::ota {
// Reject full-body fallbacks, stale identity, wrong offset or wrong length.
inline bool range_response_matches(int status,bool invalid,const char* etag,
        const char* expected_etag,const char* range,const char* expected_range,
        int64_t length,uint32_t expected_length) {
    return status==206 && !invalid && etag && expected_etag && range &&
        expected_range && std::strcmp(etag,expected_etag)==0 &&
        std::strcmp(range,expected_range)==0 && length==expected_length;
}
// Bounded segmented PSRAM image. Allocation is injected so that host
// tests exercise failure cleanup and firmware never falls back to DRAM.
template<size_t Block,size_t MaxBlocks>
class SegmentedImage {
public:
    using Alloc=void*(*)(size_t);
    using Free=void(*)(void*);
    SegmentedImage(Alloc alloc,Free release):alloc_(alloc),release_(release){}
    ~SegmentedImage(){reset();}
    SegmentedImage(const SegmentedImage&)=delete;
    SegmentedImage& operator=(const SegmentedImage&)=delete;
    bool reserve(size_t size) {
        reset();
        if(!size || size>Block*MaxBlocks)return false;
        for(size_t pos=0;pos<size;pos+=Block) {
            auto* p=static_cast<uint8_t*>(alloc_(std::min(Block,size-pos)));
            if(!p){reset();return false;}
            blocks_[allocated_++]=p;
        }
        size_=size;return true;
    }
    void reset() {
        while(allocated_){release_(blocks_[--allocated_]);blocks_[allocated_]=nullptr;}
        size_=0;
    }
    uint8_t* span(size_t pos,size_t& n) {
        if(pos>=size_){n=0;return nullptr;}
        n=std::min({n,Block-pos%Block,size_-pos});
        return blocks_[pos/Block]+pos%Block;
    }
    bool write(size_t pos,const uint8_t* data,size_t n) {
        return transfer(pos,nullptr,data,n);
    }
    bool read(size_t pos,uint8_t* data,size_t n) {
        return transfer(pos,data,nullptr,n);
    }
private:
    bool transfer(size_t pos,uint8_t* out,const uint8_t* in,size_t n) {
        if(pos>size_ || n>size_-pos || (n&&!out&&!in))return false;
        while(n) {
            size_t chunk=n;auto* p=span(pos,chunk);
            if(in){std::memcpy(p,in,chunk);in+=chunk;}
            else{std::memcpy(out,p,chunk);out+=chunk;}
            pos+=chunk;n-=chunk;
        }
        return true;
    }
    Alloc alloc_;Free release_;
    uint8_t* blocks_[MaxBlocks]{};
    size_t size_=0,allocated_=0;
};
// The transport writes only RAM. A complete length and digest are required
// before this function returns Complete. No flash or boot operations belong
// to the Source interface; the caller must close TLS before programming.
enum class StageResult {Complete,NotReady,ReadError,Stalled,WrongLength,HashError,DigestMismatch};

template<class Source>
StageResult stage_image(uint8_t* storage,size_t capacity,size_t expected,
                        uint8_t* scratch,size_t scratch_size,Source& source) {
    if(!storage || !scratch || !scratch_size || !expected || expected>capacity)
        return StageResult::WrongLength;
    size_t done=0;
    unsigned zero_reads=0;
    while(done<expected) {
        if(!source.ready())return StageResult::NotReady;
        const size_t chunk=std::min(scratch_size,expected-done);
        const int got=source.read(scratch,chunk);
        if(got<0)return StageResult::ReadError;
        if(static_cast<size_t>(got)>chunk)return StageResult::WrongLength;
        // A read may block. Recheck the original memory and link gates and
        // the absolute deadline before accepting even the last bytes.
        if(!source.ready())return StageResult::NotReady;
        if(!got) {
            if(++zero_reads>25)return StageResult::Stalled;
            source.wait();
            continue;
        }
        zero_reads=0;
        if(!source.hash(scratch,static_cast<size_t>(got)))return StageResult::HashError;
        std::memcpy(storage+done,scratch,static_cast<size_t>(got));
        done+=static_cast<size_t>(got);
        source.progress(done);
    }
    if(!source.complete())return StageResult::WrongLength;
    return source.verify();
}
}
