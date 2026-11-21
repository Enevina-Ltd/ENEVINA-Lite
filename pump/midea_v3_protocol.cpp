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
bool decode_device_id(const char* id,uint64_t& out) {
    if(!id || !*id || *id=='-' || *id=='+')return false;
    for(const char* p=id;*p;++p)if(*p<'0'||*p>'9')return false;
    errno=0; char* end=nullptr;
    const unsigned long long value=std::strtoull(id,&end,10);
    if(errno || !end || *end || value==0)return false;
    out=static_cast<uint64_t>(value);
    return true;
}
bool decode_credentials(const char* id,const char* token_hex,const char* key_hex,Credential& out) {
    if(!token_hex || !key_hex)return false;
    Credential next{};
    if(!decode_device_id(id,next.device_id))return false;
    if(!decode_hex(token_hex,next.token.data(),next.token.size()) ||
       !decode_hex(key_hex,next.key.data(),next.key.size()))return false;
    out=next;
    return true;
}
bool v2_packet_size(const uint8_t* header,size_t n,size_t& total) {
    if(!header || n<6 || header[0]!=0x5a || header[1]!=0x5a ||
       header[2]!=0x01 || header[3]!=0x11)return false;
    const size_t length=static_cast<size_t>(header[4]) |
                        (static_cast<size_t>(header[5])<<8);
    if(length<72 || length>max_packet || (length-56)%16!=0)return false;
    total=length;
    return true;
}
bool v2_packet_matches_device(const uint8_t* p,size_t n,uint64_t id) {
    size_t length=0;
    if(!id || !p || !v2_packet_size(p,n,length) || n!=length)return false;
    for(size_t i=0;i<8;++i)
        if(p[20+i]!=static_cast<uint8_t>(id>>(8*i)))return false;
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
bool valid_control_patch(const ControlPatch& patch) {
    constexpr uint8_t all=ControlPower|ControlMode|ControlTemperature|
                          ControlFan|ControlSwing;
    if(!patch.changed || (patch.changed & ~all))return false;
    if((patch.changed&ControlMode) &&
       (patch.mode<1 || patch.mode>5))return false;
    // Wire range aligned with valid_state() readback; actual model limits
    // must also be verified by the explicit per-device pilot approval.
    if(patch.changed&ControlTemperature) {
        if(!std::isfinite(patch.setpoint_c) ||
           patch.setpoint_c<12 || patch.setpoint_c>40 ||
           std::fabs(patch.setpoint_c*2-
                     std::round(patch.setpoint_c*2))>0.00001)return false;
    }
    if((patch.changed&ControlFan) && patch.fan>102)return false;
    if((patch.changed&ControlSwing) &&
       patch.swing!=0 && patch.swing!=3 &&
       patch.swing!=12 && patch.swing!=15)return false;
    return true;
}
bool set_state_command(const State& existing,const ControlPatch& patch,
                       uint8_t msg,Bytes& frame) {
    // Midea 0x40 writes the full AC state, including the optional 8 C
    // heat-preservation bit. Short C0 reads do not report this bit. Even
    // when the visible setpoint is >=16 C, its actual value is unknown on
    // some OEM firmware; never serialize a guessed zero. A model-specific
    // read or verified field-only write is required for short-C0 devices.
    // This is an optional heating preset, not the firmware's defrost logic.
    if(!valid_control_patch(patch) || !valid_state(existing) ||
       !existing.freeze_protection_known)return false;
    const bool power=(patch.changed&ControlPower)?patch.power:existing.power;
    const uint8_t mode=(patch.changed&ControlMode)?patch.mode:existing.mode;
    const double temp=(patch.changed&ControlTemperature)?
                      patch.setpoint_c:existing.setpoint_c;
    const uint8_t fan=(patch.changed&ControlFan)?patch.fan:existing.fan;
    const uint8_t swing=(patch.changed&ControlSwing)?patch.swing:existing.swing;
    if(mode<1 || mode>5 || fan>102 ||
       (swing!=0 && swing!=3 && swing!=12 && swing!=15) ||
       temp<12 || temp>40 || !std::isfinite(temp) ||
       std::fabs(temp*2-std::round(temp*2))>0.00001)
        return false;
    const int integer=static_cast<int>(temp);
    const bool half=(temp-integer)>=0.499;
    uint8_t temperature=half?0x10:0;
    uint8_t temperature_alt=0;
    if(integer>=17 && integer<=30)
        temperature|=static_cast<uint8_t>(integer-16);
    else temperature_alt=static_cast<uint8_t>(integer-12);
    temperature|=static_cast<uint8_t>(mode<<5);
    // Wire format cross-checked against independently executed MIT msmart-ng.
    Bytes body{
        0x40,
        static_cast<uint8_t>(0x02|(power?0x01:0)), // no unsolicited beep
        temperature,
        fan,
        0x7f,0x7f,0x00,
        static_cast<uint8_t>(0x30|swing),
        static_cast<uint8_t>((existing.follow_me?0x80:0)|
                             (existing.turbo?0x20:0)),
        static_cast<uint8_t>((existing.eco?0x80:0)|
                             (existing.purifier?0x20:0)|
                             (existing.aux_heat?0x08:0)),
        static_cast<uint8_t>((existing.sleep?0x01:0)|
                             (existing.turbo?0x02:0)|
                             (existing.fahrenheit?0x04:0)),
        0,0,0,0,0,0,0,
        temperature_alt,
        existing.humidity,
        0,
        static_cast<uint8_t>(existing.freeze_protection?0x80:0),
        static_cast<uint8_t>(existing.independent_aux_heat?0x08:0),0
    };
    if(body.size()!=24)return false;
    body.push_back(msg);
    body.push_back(crc8(body.data(),body.size()));
    Bytes outgoing(10,0);
    outgoing[0]=0xaa;
    outgoing[1]=static_cast<uint8_t>(10+body.size());
    outgoing[2]=0xac;
    outgoing[9]=0x02;
    outgoing.insert(outgoing.end(),body.begin(),body.end());
    outgoing.push_back(checksum(outgoing.data()+1,outgoing.size()-1));
    frame.swap(outgoing);
    return true;
}
bool control_patch_observed(const State& actual,const ControlPatch& patch) {
    if(!valid_control_patch(patch) || !valid_state(actual))return false;
    if((patch.changed&ControlPower) && actual.power!=patch.power)return false;
    if((patch.changed&ControlMode) && actual.mode!=patch.mode)return false;
    if((patch.changed&ControlTemperature) &&
       std::fabs(actual.setpoint_c-patch.setpoint_c)>0.26)return false;
    if((patch.changed&ControlFan) && actual.fan!=patch.fan)return false;
    if((patch.changed&ControlSwing) && actual.swing!=patch.swing)return false;
    return true;
}
bool preserved_state_observed(const State& before,const State& after,
                              const ControlPatch& patch) {
    if(!valid_control_patch(patch) ||
       !valid_state(before) || !valid_state(after) ||
       // Do not report confirmed state when the post-write C0 omits the
       // preserved flag: matching visible setpoints cannot prove its value.
       !before.freeze_protection_known || !after.freeze_protection_known ||
       before.freeze_protection!=after.freeze_protection ||
       before.independent_aux_heat!=after.independent_aux_heat ||
       before.aux_heat!=after.aux_heat)return false;
    // No false success if the controller has silently reset a different
    // basic setting while applying the requested field.
    if(!(patch.changed&ControlPower) && before.power!=after.power)return false;
    if(!(patch.changed&ControlMode) && before.mode!=after.mode)return false;
    if(!(patch.changed&ControlTemperature) &&
       std::fabs(before.setpoint_c-after.setpoint_c)>0.26)return false;
    if(!(patch.changed&ControlFan) && before.fan!=after.fan)return false;
    if(!(patch.changed&ControlSwing) && before.swing!=after.swing)return false;
    if(before.eco!=after.eco || before.sleep!=after.sleep ||
       before.purifier!=after.purifier ||
       before.follow_me!=after.follow_me ||
       before.turbo!=after.turbo ||
       before.fahrenheit!=after.fahrenheit)return false;
    return true;
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
uint8_t parse_state_failure_code(const uint8_t* f,size_t n) {
    if(!f || n<10+17+2 || n>256)return 1;
    if(f[0]!=0xaa || f[2]!=0xac)return 2;
    if(static_cast<size_t>(f[1])+1!=n)return 3;
    if(f[10]!=0xc0)return 4;
    if(checksum(f+1,n-2)!=f[n-1])return 5;
    const size_t paylen=n-12;
    if(paylen<17)return 1;
    const uint8_t* payload=f+10;
    const uint8_t crc=crc8(payload,paylen);
    if(payload[paylen]!=crc && payload[paylen]!=checksum(payload,paylen))
        return 6;
    State parsed{};
    return parse_state(f,n,parsed)?0:7;
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
    s.independent_aux_heat=(p[8]&0x40)!=0;
    s.classic_aux_bit_ambiguous=s.independent_aux_heat;
    s.independent_aux_heat_known=!s.classic_aux_bit_ambiguous;
    s.follow_me=(p[8]&0x80)!=0; s.eco=(p[9]&0x10)!=0;
    s.follow_me_known=true;
    s.aux_heat=(p[9]&8)!=0; s.purifier=(p[9]&0x20)!=0;
    s.sleep=(p[10]&1)!=0; s.fahrenheit=(p[10]&4)!=0;
    s.fahrenheit_known=true;
    s.has_indoor= temp_byte(p[11],paylen>15?p[15]&15:0,s.fahrenheit,s.indoor_c);
    s.has_outdoor=temp_byte(p[12],paylen>15?p[15]>>4:0,s.fahrenheit,s.outdoor_c);
    if(paylen>19) { s.humidity=p[19]&127;s.target_humidity_known=true; }
    if(paylen>21) {
        s.freeze_protection_known=true;
        s.freeze_protection=(p[21]&0x80)!=0;
    }
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
