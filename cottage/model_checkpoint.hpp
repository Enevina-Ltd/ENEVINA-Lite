#pragma once
#include "cottage/history_persist.hpp"
#include "cottage/building_identification.hpp"
#include <algorithm>
#include <cstring>

namespace enevina::cottage {
// Model blobs already carry schema, identity, epoch and CRC. Reuse the
// existing atomic-file backend instead of competing with configuration/OTA
// keys in the small NVS partition. No model-specific flash partition.
enum class CheckpointRead : uint8_t { Missing, Read, Error };
inline CheckpointRead read_model_checkpoint(FileBackend& file,uint8_t* out,
        size_t capacity,size_t& used) {
    used=0;
    if(!file.exists())return CheckpointRead::Missing;
    const size_t n=file.size();
    if(!out || !n || n>capacity || n>BuildingIdentifier::blob_capacity ||
       !file.read(0,out,n))return CheckpointRead::Error;
    used=n;return CheckpointRead::Read;
}
inline bool write_model_checkpoint(FileBackend& file,const uint8_t* data,size_t n) {
    if(!data || !n || n>BuildingIdentifier::blob_capacity)return false;
    if(!file.replace_begin())return false;
    if(!file.replace_write(data,n) || !file.replace_commit()) {
        file.replace_abort();return false;
    }
    if(!file.exists() || file.size()!=n)return false;
    // Verify exact committed bytes, without another full model allocation.
    uint8_t chunk[128];
    for(size_t i=0;i<n;) {
        const size_t k=std::min(sizeof(chunk),n-i);
        if(!file.read(i,chunk,k) || std::memcmp(chunk,data+i,k)!=0)return false;
        i+=k;
    }
    return true;
}
} // namespace enevina::cottage
