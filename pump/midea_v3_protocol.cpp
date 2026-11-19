#include "pump/midea_v3_protocol.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <mbedtls/aes.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>
#else
#include <openssl/evp.h>
#endif

// Midea proprietary 8370 wire interoperability. Adapted from protocol
// descriptions and interoperability tests of mill1000/msmart-ng (MIT).
// It is NOT a replacement for pump-specific field testing.
namespace enevina::midea {
namespace {
constexpr uint8_t sign_key[]="xhdiwjnchekd4d512chdjx5d8e4c394D2D7S";
constexpr size_t max_packet=1024;
bool equals(const uint8_t* a,const uint8_t* b,size_t n) {
    uint8_t diff=0;
    for(size_t i=0;i<n;++i)diff|=a[i]^b[i];
    return diff==0;
}
bool hash(const uint8_t* data,size_t size,bool sha256,uint8_t* out) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    return sha256 ? mbedtls_sha256_ret(data,size,out,0)==0
                  : mbedtls_md5_ret(data,size,out)==0;
#else
    unsigned int actual=0;
    return EVP_Digest(data,size,out,&actual,sha256?EVP_sha256():EVP_md5(),nullptr)==1
        && actual==(sha256?32u:16u);
#endif
}
bool aes(const uint8_t* key,bool aes256,bool cbc,bool enc,
         const uint8_t* data,size_t size,uint8_t* out) {
    if((size%16)!=0)return false;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    const int bits=aes256?256:128;
    int ret=enc?mbedtls_aes_setkey_enc(&ctx,key,bits):
                mbedtls_aes_setkey_dec(&ctx,key,bits);
    if(ret==0 && cbc) {
        uint8_t iv[16]{};
        ret=mbedtls_aes_crypt_cbc(&ctx,enc?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,
                                   size,iv,data,out);
    } else if(ret==0) {
        for(size_t p=0;p<size;p+=16) {
            ret=mbedtls_aes_crypt_ecb(&ctx,enc?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,
                                      data+p,out+p);
            if(ret!=0)break;
        }
    }
    mbedtls_aes_free(&ctx);
    return ret==0;
#else
    EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new();
    if(!ctx)return false;
    const EVP_CIPHER* cipher = cbc?(aes256?EVP_aes_256_cbc():EVP_aes_128_cbc()):
                                   (aes256?EVP_aes_256_ecb():EVP_aes_128_ecb());
    uint8_t iv[16]{};
    int n=0,tail=0;
    bool ok=EVP_CipherInit_ex(ctx,cipher,nullptr,key,cbc?iv:nullptr,enc?1:0)==1
      && EVP_CIPHER_CTX_set_padding(ctx,0)==1
      && EVP_CipherUpdate(ctx,out,&n,data,static_cast<int>(size))==1
      && EVP_CipherFinal_ex(ctx,out+n,&tail)==1
      && static_cast<size_t>(n+tail)==size;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
#endif
}
bool fixed_sign(const uint8_t* data,size_t n,uint8_t out[16]) {
    Bytes to_sign(data,data+n);
    to_sign.insert(to_sign.end(),sign_key,sign_key+sizeof(sign_key)-1);
    return hash(to_sign.data(),to_sign.size(),false,out);
}
bool fixed_enc_key(uint8_t out[16]) {
    return hash(sign_key,sizeof(sign_key)-1,false,out);
}
uint8_t crc8(const uint8_t* p,size_t n) {
    // Dallas/Maxim CRC-8 reflected polynomial 0x8c, same as Midea's 0x854 table.
    uint8_t crc=0;
    for(size_t i=0;i<n;++i) {
        crc^=p[i];
        for(int k=0;k<8;++k)crc=(crc&1)?((crc>>1)^0x8c):(crc>>1);
    }
    return crc;
}
uint8_t checksum(const uint8_t* p,size_t n) {
    uint8_t sum=0;
    for(size_t i=0;i<n;++i)sum=static_cast<uint8_t>(sum+p[i]);
    return static_cast<uint8_t>(-sum);
}
bool temp_byte(uint8_t raw,uint8_t precision,bool fahrenheit,double& out) {
    if(raw==0xff)return false;
    const double base=(static_cast<int>(raw)-50)/2.0;
    const double d=(precision&15)/10.0;
    if(!fahrenheit && d>0)out=std::trunc(base)+(base>=0?d:-d);
    else if(d>=0.5)out=std::trunc(base)+(base>=0?0.5:-0.5);
    else out=base;
    return std::isfinite(out) && out>-80 && out<100;
}
} // namespace

bool decode_hex(const char* hex,uint8_t* dst,size_t count) {
    if(!hex || !dst || std::strlen(hex)!=count*2)return false;
    for(size_t i=0;i<count;++i) {
        const auto nibble=[](char c)->int {
            if(c>='0'&&c<='9')return c-'0';
            if(c>='a'&&c<='f')return c-'a'+10;
            if(c>='A'&&c<='F')return c-'A'+10;
            return -1;
        };
        int a=nibble(hex[i*2]),b=nibble(hex[i*2+1]);
        if(a<0 || b<0)return false;
        dst[i]=static_cast<uint8_t>(a*16+b);
    }
    return true;
}
bool decode_credentials(const char* id,const char* token_hex,const char* key_hex,Credential& out) {
    if(!id || !*id || !token_hex || !key_hex || *id=='-' || *id=='+')return false;
    for(const char* p=id;*p;++p)if(*p<'0'||*p>'9')return false;
    errno=0; char* end=nullptr;
    unsigned long long value=std::strtoull(id,&end,10);
    if(errno || !end || *end || value==0)return false;
    Credential next{};
    next.device_id=static_cast<uint64_t>(value);
    if(!decode_hex(token_hex,next.token.data(),next.token.size()) ||
       !decode_hex(key_hex,next.key.data(),next.key.size()))return false;
    out=next;
    return true;
}
bool packet_size(const uint8_t* header,size_t n,size_t& total) {
    if(!header || n<6 || header[0]!=0x83 || header[1]!=0x70 || header[4]!=0x20)
        return false;
    total=(static_cast<size_t>(header[2])<<8 | header[3])+8;
    return total>=8 && total<=max_packet;
}
Bytes handshake_request(const Credential& c,uint16_t sequence) {
    Bytes p{0x83,0x70,0,64,0x20,0,static_cast<uint8_t>(sequence>>8),
            static_cast<uint8_t>(sequence)};
    p.insert(p.end(),c.token.begin(),c.token.end());
    return p;
}
bool handshake_response(const uint8_t* p,size_t n,const Credential& c,
                        std::array<uint8_t,32>& session) {
    size_t total=0;
    if(!packet_size(p,n,total) || total!=72 || n!=total || (p[5]&15)!=1 ||
       (p[5]>>4)!=0)return false;
    std::array<uint8_t,32> decoded{};
    if(!aes(c.key.data(),true,true,false,p+8,32,decoded.data()))return false;
    uint8_t calc[32]{};
    if(!hash(decoded.data(),decoded.size(),true,calc) ||
       !equals(calc,p+40,32))return false;
    for(size_t i=0;i<32;++i)decoded[i]^=c.key[i];
    session=decoded;
    return true;
}
Bytes state_query(uint8_t msg) {
    // AC Frame QUERY 0x03: state query 0x41 with indoor temperature.
    Bytes payload{0x41,0x81,0,0xff,0x03,0xff,0,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0x03};
    payload.push_back(msg);
    payload.push_back(crc8(payload.data(),payload.size()));
    Bytes frame(10,0);
    frame[0]=0xaa;
    frame[1]=static_cast<uint8_t>(10+payload.size());
    frame[2]=0xac;
    frame[9]=0x03;
    frame.insert(frame.end(),payload.begin(),payload.end());
    frame.push_back(checksum(frame.data()+1,frame.size()-1));
    return frame;
}
bool inner_encode(uint64_t id,const uint8_t* command,size_t n,
                  const uint8_t stamp[8],Bytes& out) {
    if(!command || !stamp || !n || n>256 || id==0)return false;
    uint8_t k[16]{};
    if(!fixed_enc_key(k))return false;
    const size_t pad=16-(n%16); // PKCS#7; full block when aligned.
    Bytes plain(command,command+n);
    plain.insert(plain.end(),pad,static_cast<uint8_t>(pad));
    Bytes encrypted(plain.size());
    if(!aes(k,false,false,true,plain.data(),plain.size(),encrypted.data()))return false;
    const size_t total=40+encrypted.size()+16;
    if(total>max_packet)return false;
    Bytes p(40,0);
    p[0]=0x5a;p[1]=0x5a;p[2]=0x01;p[3]=0x11;
    p[4]=static_cast<uint8_t>(total);p[5]=static_cast<uint8_t>(total>>8);
    p[6]=0x20;
    std::memcpy(p.data()+12,stamp,8);
    for(size_t i=0;i<8;++i)p[20+i]=static_cast<uint8_t>(id>>(8*i));
    p.insert(p.end(),encrypted.begin(),encrypted.end());
    uint8_t sign[16]{};
    if(!fixed_sign(p.data(),p.size(),sign))return false;
    p.insert(p.end(),sign,sign+16);
    out.swap(p);
    return true;
}
bool inner_decode(const uint8_t* p,size_t n,Bytes& frame) {
    if(!p || n<72 || n>max_packet || p[0]!=0x5a || p[1]!=0x5a ||
       p[2]!=0x01 || p[3]!=0x11)return false;
    const size_t plen=p[4] | static_cast<size_t>(p[5])<<8;
    if(plen!=n || (n-56)%16!=0)return false;
    uint8_t sign[16]{};
    if(!fixed_sign(p,n-16,sign) || !equals(sign,p+n-16,16))return false;
    uint8_t k[16]{};
    if(!fixed_enc_key(k))return false;
    Bytes decoded(n-56);
    if(!aes(k,false,false,false,p+40,decoded.size(),decoded.data()))return false;
    if(decoded.empty())return false;
    const uint8_t pad=decoded.back();
    if(pad==0 || pad>16 || pad>decoded.size())return false;
    for(size_t i=decoded.size()-pad;i<decoded.size();++i)
        if(decoded[i]!=pad)return false;
    decoded.resize(decoded.size()-pad);
    frame.swap(decoded);
    return true;
}
bool encrypted_request(const uint8_t* inner,size_t n,const std::array<uint8_t,32>& key,
                       uint16_t seq,const uint8_t* pad_data,size_t pad_capacity,Bytes& out) {
    if(!inner || n==0 || n>max_packet-96)return false;
    const size_t pad=(16-((n+2)%16))%16;
    if(pad && (!pad_data || pad_capacity<pad))return false;
    const size_t length=n+pad+32;
    if(length+8>max_packet)return false;
    Bytes packet{0x83,0x70,static_cast<uint8_t>(length>>8),
                 static_cast<uint8_t>(length),0x20,
                 static_cast<uint8_t>((pad<<4)|6)};
    Bytes plain{static_cast<uint8_t>(seq>>8),static_cast<uint8_t>(seq)};
    plain.insert(plain.end(),inner,inner+n);
    if(pad)plain.insert(plain.end(),pad_data,pad_data+pad);
    uint8_t sign[32]{};
    Bytes signed_plain=packet;
    signed_plain.insert(signed_plain.end(),plain.begin(),plain.end());
    if(!hash(signed_plain.data(),signed_plain.size(),true,sign))return false;
    Bytes ciphertext(plain.size());
    if(!aes(key.data(),true,true,true,plain.data(),plain.size(),ciphertext.data()))return false;
    packet.insert(packet.end(),ciphertext.begin(),ciphertext.end());
    packet.insert(packet.end(),sign,sign+32);
    out.swap(packet);
    return true;
}
bool encrypted_response(const uint8_t* p,size_t n,
                        const std::array<uint8_t,32>& key,Bytes& inner) {
    size_t total=0;
    if(!packet_size(p,n,total) || total!=n || n<56 ||
       (p[5]&15)!=3)return false;
    const size_t pad=p[5]>>4;
    const size_t cipher_len=n-6-32;
    if(cipher_len<16 || cipher_len%16!=0 || pad>15 ||
       cipher_len<2+pad)return false;
    Bytes plain(cipher_len);
    if(!aes(key.data(),true,true,false,p+6,cipher_len,plain.data()))return false;
    Bytes signed_plain(p,p+6);
    signed_plain.insert(signed_plain.end(),plain.begin(),plain.end());
    uint8_t sign[32]{};
    if(!hash(signed_plain.data(),signed_plain.size(),true,sign) ||
       !equals(sign,p+6+cipher_len,32))return false;
    Bytes candidate(plain.begin()+2,plain.end()-pad);
    inner.swap(candidate);
    return true;
}
bool parse_state(const uint8_t* f,size_t n,State& out) {
    if(!f || n<10+17+2 || n>256 || f[0]!=0xaa || f[2]!=0xac ||
       static_cast<size_t>(f[1])+1!=n || f[10]!=0xc0 ||
       checksum(f+1,n-2)!=f[n-1])return false;
    const size_t paylen=n-12; // payload content excluding CRC and frame checksum
    if(paylen<17)return false;
    const uint8_t* p=f+10;
    const uint8_t expected_crc=crc8(p,paylen);
    if(p[paylen]!=expected_crc && p[paylen]!=checksum(p,paylen))return false;
    State s{};
    s.power=(p[1]&1)!=0;
    s.mode=(p[2]>>5)&7;
    s.setpoint_c=(p[2]&15)+16.0+((p[2]&16)?0.5:0);
    if(paylen>13 && (p[13]&31))s.setpoint_c=(p[13]&31)+12+((p[2]&16)?0.5:0);
    s.fan=p[3]&0x7f; s.swing=p[7]&15;
    s.turbo=(p[8]&0x20)!=0 || (p[10]&2)!=0;
    s.follow_me=(p[8]&0x80)!=0; s.eco=(p[9]&0x10)!=0;
    s.aux_heat=(p[9]&8)!=0; s.purifier=(p[9]&0x20)!=0;
    s.sleep=(p[10]&1)!=0; s.fahrenheit=(p[10]&4)!=0;
    s.has_indoor= temp_byte(p[11],paylen>15?p[15]&15:0,s.fahrenheit,s.indoor_c);
    s.has_outdoor=temp_byte(p[12],paylen>15?p[15]>>4:0,s.fahrenheit,s.outdoor_c);
    if(paylen>19)s.humidity=p[19]&127;
    if(paylen>21)s.freeze_protection=(p[21]&0x80)!=0;
    if(!valid_state(s))return false;
    out=s;
    return true;
}
bool valid_state(const State& s) {
    return s.mode<=5 && s.setpoint_c>=10 && s.setpoint_c<=40 &&
           std::isfinite(s.setpoint_c) &&
           (!s.has_indoor || (std::isfinite(s.indoor_c)&&s.indoor_c>-60&&s.indoor_c<80)) &&
           (!s.has_outdoor || (std::isfinite(s.outdoor_c)&&s.outdoor_c>-80&&s.outdoor_c<80));
}
} // namespace enevina::midea