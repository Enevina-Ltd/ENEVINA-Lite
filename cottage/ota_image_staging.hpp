#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enevina::cottage::ota {
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
