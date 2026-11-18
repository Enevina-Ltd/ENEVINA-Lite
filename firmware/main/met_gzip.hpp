#pragma once
// MET Norway explicitly requests gzip/deflate support. Use ESP32-S3 ROM tinfl
// with buffers in PSRAM instead of importing a second compression library.
// Validate the GZIP footer (CRC32 + ISIZE) before exposing decompressed JSON.
#include <cstddef>
#include <cstdint>
#include <cstring>
#ifdef ARDUINO
#include <esp32s3/rom/miniz.h>
#endif
namespace enevina_firmware {
inline uint32_t met_gzip_le32(const uint8_t* p) {
    return uint32_t(p[0])|uint32_t(p[1])<<8|
           uint32_t(p[2])<<16|uint32_t(p[3])<<24;
}
inline bool met_gzip_payload(const uint8_t* input,size_t length,
                             size_t& from,size_t& to,uint32_t& crc,
                             uint32_t& decompressed) {
    if(!input||length<18||input[0]!=0x1f||input[1]!=0x8b||
       input[2]!=8|| (input[3]&0xe0)!=0)return false;
    const uint8_t flags=input[3];
    size_t at=10;
    const size_t end=length-8;
    if(flags&4) {
        if(at+2>end)return false;
        const size_t extra=input[at]|size_t(input[at+1])<<8;
        at+=2;if(extra>end-at)return false;at+=extra;
    }
    for(const uint8_t bit:{uint8_t(8),uint8_t(16)}) {
        if(!(flags&bit))continue;
        while(at<end && input[at]!=0)++at;
        if(at>=end)return false;
        ++at;
    }
    if(flags&2) {if(at+2>end)return false;at+=2;}
    if(at>=end)return false;
    from=at;to=end;
    crc=met_gzip_le32(input+end);
    decompressed=met_gzip_le32(input+end+4);
    return true;
}
#ifdef ARDUINO
inline bool met_expand_http(const char* encoding,const uint8_t* src,size_t used,
                            char* out,size_t out_capacity,size_t& decoded) {
    decoded=0;
    if(!src||!out||out_capacity<2)return false;
    if(!encoding || !*encoding || std::strcmp(encoding,"identity")==0){
        if(used>=out_capacity)return false;
        std::memcpy(out,src,used);
        out[used]='\0';decoded=used;return true;
    }
    if(std::strcmp(encoding,"gzip")==0) {
        size_t from=0,to=0;uint32_t crc=0,isize=0;
        if(!met_gzip_payload(src,used,from,to,crc,isize)||
           isize>=out_capacity)return false;
        const size_t produced=tinfl_decompress_mem_to_mem(
            out,out_capacity-1,src+from,to-from,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if(produced==TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ||
           produced!=isize)return false;
        if(static_cast<uint32_t>(
            mz_crc32(MZ_CRC32_INIT,reinterpret_cast<const unsigned char*>(out),
                     produced))!=crc)return false;
        out[produced]='\0';decoded=produced;return true;
    }
    if(std::strcmp(encoding,"deflate")==0) {
        const size_t produced=tinfl_decompress_mem_to_mem(
            out,out_capacity-1,src,used,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF |
            TINFL_FLAG_PARSE_ZLIB_HEADER);
        if(produced==TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)return false;
        out[produced]='\0';decoded=produced;return true;
    }
    return false; // Unknown compression: never parse or trust bytes.
}
#endif
}
