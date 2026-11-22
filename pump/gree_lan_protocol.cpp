#include "pump/gree_lan_protocol.hpp"
#include <algorithm>
#include <cJSON.h>
#include <memory>
#ifdef ARDUINO
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#else
#include <openssl/evp.h>
#endif

namespace enevina::gree {
namespace {
using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
using Tag=std::array<uint8_t,16>;
constexpr size_t max_inner=900;
constexpr unsigned char nonce[]={0x54,0x40,0x78,0x44,0x49,0x67,0x5a,0x51,0x6c,0x5e,0x63,0x13};
constexpr unsigned char aad[]="qualcomm-test";
constexpr Key ecb_default={'a','3','K','8','B','x','%','2','r','8','Y','7','#','x','D','h'};
constexpr Key gcm_default={'{','y','x','A','H','A','Y','_','L','m','6','p','b','C','/','<'};

Json parse(std::string_view text,size_t limit) {
    Json doc(nullptr,cJSON_Delete);
    if(text.empty() || text.size()>limit || text.find('\0')!=text.npos)return doc;
    // Limit parser recursion on a bounded RTOS stack and reject C-string truncation.
    unsigned depth=0;
    bool quoted=false,escaped=false;
    for(size_t i=0;i<text.size();++i) {
        const char c=text[i];
        if(quoted) {
            if(escaped) {
                if(text.substr(i,5)=="u0000")return doc;
                escaped=false;
            } else if(c=='\\')escaped=true;
            else if(c=='"')quoted=false;
        } else if(c=='"')quoted=true;
        else if(c=='{' || c=='[') {
            if(++depth>5)return doc;
        } else if(c=='}' || c==']') {
            if(depth==0)return doc;
            --depth;
        }
    }
    if(depth || quoted)return doc;
    const std::string input(text);
    doc.reset(cJSON_ParseWithLengthOpts(input.c_str(),input.size()+1,nullptr,true));
    if(!cJSON_IsObject(doc.get()))return Json(nullptr,cJSON_Delete);
    for(const cJSON* item=doc->child;item;item=item->next) {
        if(!item->string || cJSON_GetObjectItemCaseSensitive(doc.get(),item->string)!=item)
            return Json(nullptr,cJSON_Delete);
    }
    return doc;
}

const cJSON* field(const cJSON* obj,const char* name) {
    return cJSON_GetObjectItemCaseSensitive(obj,name);
}
std::optional<std::string_view> str(const cJSON* obj,const char* name) {
    const auto* item=field(obj,name);
    if(!cJSON_IsString(item) || !item->valuestring)return {};
    return item->valuestring;
}
std::optional<int> integer(const cJSON* item) {
    if(!cJSON_IsNumber(item) || item->valuedouble!=item->valueint)return {};
    return item->valueint;
}

bool identity(std::string_view observed,std::string_view expected) {
    if(expected=="app" || expected.empty())return observed==expected;
    const auto id=device_id(observed);
    return id && id==device_id(expected);
}
bool inner_id(const cJSON* obj,std::string_view expected) {
    for(const auto* key:{"mac","cid"}) {
        if(!field(obj,key))continue;
        const auto value=str(obj,key);
        if(!value || !identity(*value,expected))return false;
    }
    return true;
}

std::string base64(std::string_view bytes) {
    const auto* src=reinterpret_cast<const unsigned char*>(bytes.data());
    std::string out(4*((bytes.size()+2)/3)+1,'\0');
    auto* dst=reinterpret_cast<unsigned char*>(out.data());
#ifdef ARDUINO
    size_t size=0;
    if(mbedtls_base64_encode(dst,out.size(),&size,src,bytes.size())!=0)return {};
#else
    const size_t size=EVP_EncodeBlock(dst,src,bytes.size());
#endif
    out.resize(size);
    return out;
}

std::optional<std::string> unbase64(std::string_view encoded) {
    if(encoded.empty() || encoded.size()%4)return {};
    const auto* src=reinterpret_cast<const unsigned char*>(encoded.data());
    std::string out(encoded.size()/4*3,'\0');
    auto* dst=reinterpret_cast<unsigned char*>(out.data());
#ifdef ARDUINO
    size_t size=0;
    if(mbedtls_base64_decode(dst,out.size(),&size,src,encoded.size())!=0)return {};
#else
    const int n=EVP_DecodeBlock(dst,src,encoded.size());
    if(n<0)return {};
    const size_t pad=(encoded.back()=='=')+(encoded[encoded.size()-2]=='=');
    if(static_cast<size_t>(n)<pad)return {};
    const size_t size=n-pad;
#endif
    out.resize(size);
    if(out.empty() || base64(out)!=encoded)return {};
    return out;
}

bool crypt(Cipher version,const Key& key,bool encrypt,std::string_view bytes,
           std::string& result,Tag& tag) {
    const bool gcm=version==Cipher::Gcm;
    if(bytes.empty() || (!gcm && (version!=Cipher::Ecb || bytes.size()%16)))return false;
    const auto* src=reinterpret_cast<const unsigned char*>(bytes.data());
    result.resize(bytes.size()+16);
    auto* dst=reinterpret_cast<unsigned char*>(result.data());
#ifdef ARDUINO
    int error=0;
    if(gcm) {
        mbedtls_gcm_context ctx;
        mbedtls_gcm_init(&ctx);
        error=mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key.data(),128);
        if(!error && encrypt)
            error=mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,bytes.size(),nonce,sizeof(nonce),
                aad,sizeof(aad)-1,src,dst,tag.size(),tag.data());
        if(!error && !encrypt)
            error=mbedtls_gcm_auth_decrypt(&ctx,bytes.size(),nonce,sizeof(nonce),
                aad,sizeof(aad)-1,tag.data(),tag.size(),src,dst);
        mbedtls_gcm_free(&ctx);
    } else {
        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        error=encrypt?mbedtls_aes_setkey_enc(&ctx,key.data(),128):
                      mbedtls_aes_setkey_dec(&ctx,key.data(),128);
        for(size_t i=0;!error && i<bytes.size();i+=16)
            error=mbedtls_aes_crypt_ecb(&ctx,encrypt?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,src+i,dst+i);
        mbedtls_aes_free(&ctx);
    }
    if(error)return false;
#else
    const std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)>
        ctx(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    int size=0,tail=0;
    if(!ctx || EVP_CipherInit_ex(ctx.get(),gcm?EVP_aes_128_gcm():EVP_aes_128_ecb(),
                                nullptr,key.data(),gcm?nonce:nullptr,encrypt)!=1)return false;
    if(gcm && EVP_CipherUpdate(ctx.get(),nullptr,&size,aad,sizeof(aad)-1)!=1)return false;
    if(!gcm && EVP_CIPHER_CTX_set_padding(ctx.get(),0)!=1)return false;
    if(EVP_CipherUpdate(ctx.get(),dst,&size,src,bytes.size())!=1)return false;
    if(gcm && !encrypt && EVP_CIPHER_CTX_ctrl(ctx.get(),EVP_CTRL_GCM_SET_TAG,tag.size(),tag.data())!=1)
        return false;
    if(EVP_CipherFinal_ex(ctx.get(),dst+size,&tail)!=1 ||
       static_cast<size_t>(size+tail)!=bytes.size())return false;
    if(gcm && encrypt && EVP_CIPHER_CTX_ctrl(ctx.get(),EVP_CTRL_GCM_GET_TAG,tag.size(),tag.data())!=1)
        return false;
#endif
    result.resize(bytes.size());
    return true;
}

bool header(const cJSON* obj,bool enroll) {
    return obj && str(obj,"t")=="pack" && integer(field(obj,"uid"))==0 &&
        integer(field(obj,"i"))==(enroll?1:0);
}
std::optional<std::string> decipher(const cJSON* obj,Cipher version,const Key& key) {
    const auto encoded=str(obj,"pack");
    const auto bytes=encoded?unbase64(*encoded):std::nullopt;
    if(!bytes || bytes->size()>max_inner+16)return {};
    Tag tag{};
    if(version==Cipher::Gcm) {
        const auto encoded_tag=str(obj,"tag");
        const auto tag_bytes=encoded_tag?unbase64(*encoded_tag):std::nullopt;
        if(!tag_bytes || tag_bytes->size()!=tag.size())return {};
        std::copy(tag_bytes->begin(),tag_bytes->end(),tag.begin());
    } else if(field(obj,"tag"))return {};
    std::string plain;
    if(!crypt(version,key,false,*bytes,plain,tag))return {};
    if(version==Cipher::Ecb) {
        const auto padding=static_cast<unsigned char>(plain.back());
        if(!padding || padding>16 || !std::all_of(plain.end()-padding,plain.end(),
            [padding](unsigned char c){return c==padding;}))return {};
        plain.resize(plain.size()-padding);
    }
    if(plain.empty() || plain.size()>max_inner)return {};
    return plain;
}
}

std::optional<std::string> device_id(std::string_view text) {
    const bool separated=text.size()==17;
    if(text.size()!=12 && !separated)return {};
    std::string result;
    for(size_t i=0;i<text.size();++i) {
        const char c=text[i];
        if(separated && i%3==2) {
            if(c!=text[2] || (c!=':' && c!='-'))return {};
        } else if((c>='a' && c<='f') || (c>='0' && c<='9'))result+=c;
        else if(c>='A' && c<='F')result+=static_cast<char>(c+'a'-'A');
        else return {};
    }
    if(result=="000000000000" || result=="ffffffffffff")return {};
    return result;
}
const Key& binding_key(Cipher cipher) {
    return cipher==Cipher::Gcm?gcm_default:ecb_default;
}

std::string pack(Cipher cipher,const Key& key,bool enrollment,std::string_view from,
                 std::string_view to,std::string_view payload) {
    const auto peer=[](std::string_view s){return s.empty() || s=="app" || device_id(s).has_value();};
    if(payload.empty() || payload.size()>max_inner || !peer(from) || !peer(to))return {};
    std::string padded(payload),ciphertext;
    if(cipher==Cipher::Ecb) {
        const auto padding=16-payload.size()%16;
        padded.append(padding,static_cast<char>(padding));
    }
    Tag tag{};
    if(!crypt(cipher,key,true,padded,ciphertext,tag))return {};
    std::string json="{\"t\":\"pack\",\"uid\":0,\"i\":";
    json+=enrollment?'1':'0';
    json+=",\"cid\":\"";json+=from;
    json+="\",\"tcid\":\"";json+=to;
    json+="\",\"pack\":\""+base64(ciphertext)+"\"";
    if(cipher==Cipher::Gcm)
        json+=",\"tag\":\""+base64({reinterpret_cast<const char*>(tag.data()),tag.size()})+"\"";
    json+='}';
    return json.size()<=max_packet?json:std::string{};
}
std::optional<std::string> unpack(Cipher cipher,const Key& key,bool enrollment,
                 std::string_view from,std::string_view to,std::string_view datagram) {
    const auto doc=parse(datagram,max_packet);
    if(!header(doc.get(),enrollment))return {};
    const auto src=str(doc.get(),"cid"),dst=str(doc.get(),"tcid");
    if(!src || !identity(*src,from) || !dst || !identity(*dst,to))return {};
    return decipher(doc.get(),cipher,key);
}

std::optional<Discovery> discovery(std::string_view datagram) {
    const auto doc=parse(datagram,max_packet);
    if(!header(doc.get(),true) || str(doc.get(),"tcid")!="")return {};
    const auto src=str(doc.get(),"cid");
    const auto id=src?device_id(*src):std::nullopt;
    if(!id)return {};
    const auto cipher=field(doc.get(),"tag")?Cipher::Gcm:Cipher::Ecb;
    const auto plaintext=decipher(doc.get(),cipher,binding_key(cipher));
    if(!plaintext)return {};
    const auto inner=parse(*plaintext,max_inner);
    if(!inner || str(inner.get(),"t")!="dev")return {};
    auto wire=str(inner.get(),"mac");
    if(!wire || wire->empty())wire=str(inner.get(),"cid");
    if(!wire || !identity(*wire,*id))return {};
    return Discovery{cipher,*id,std::string(*wire)};
}
std::optional<Key> bound_key(Cipher cipher,std::string_view device,std::string_view datagram) {
    const auto plain=unpack(cipher,binding_key(cipher),true,device,"app",datagram);
    if(!plain)return {};
    const auto doc=parse(*plain,max_inner);
    if(!doc || str(doc.get(),"t")!="bindok" || !inner_id(doc.get(),device))return {};
    if(const auto* result=field(doc.get(),"r");result && integer(result)!=200)return {};
    const auto value=str(doc.get(),"key");
    Key key{};
    if(!value || value->size()!=key.size() ||
       !std::all_of(value->begin(),value->end(),[](unsigned char c){return c>=32 && c<=126;}))
        return {};
    std::copy(value->begin(),value->end(),key.begin());
    return key;
}
std::string bind_request(Cipher cipher,std::string_view device) {
    if(!device_id(device))return {};
    return pack(cipher,binding_key(cipher),true,"app",device,
                "{\"t\":\"bind\",\"mac\":\""+std::string(device)+"\",\"uid\":0}");
}
std::string status_request(Cipher cipher,const Key& key,std::string_view device) {
    if(!device_id(device))return {};
    return pack(cipher,key,false,"app",device,"{\"t\":\"status\",\"mac\":\""+
                std::string(device)+"\",\"cols\":[\"Pow\",\"Mod\",\"TemUn\",\"SetTem\",\"TemSen\",\"StHt\"]}");
}
std::optional<Reading> parse_status(Cipher cipher,const Key& key,std::string_view device,
                                    std::string_view datagram) {
    const auto plain=unpack(cipher,key,false,device,"app",datagram);
    if(!plain)return {};
    const auto doc=parse(*plain,max_inner);
    if(!doc || str(doc.get(),"t")!="dat" || !inner_id(doc.get(),device))return {};
    const auto* columns=field(doc.get(),"cols");
    const auto* data=field(doc.get(),"dat");
    if(!cJSON_IsArray(columns) || !cJSON_IsArray(data) ||
       cJSON_GetArraySize(columns)!=cJSON_GetArraySize(data) || cJSON_GetArraySize(columns)>32)
        return {};
    constexpr std::array<std::string_view,6> names={"Pow","Mod","TemUn","SetTem","TemSen","StHt"};
    std::array<std::optional<int>,names.size()> values{};
    unsigned seen=0;
    for(auto* k=columns->child,* v=data->child;k;k=k->next,v=v->next) {
        if(!cJSON_IsString(k) || !k->valuestring)return {};
        const auto it=std::find(names.cbegin(),names.cend(),std::string_view(k->valuestring));
        if(it==names.cend())continue;
        const size_t index=it-names.cbegin();
        const unsigned bit=1u<<index;
        if(seen&bit)return {};
        seen|=bit;
        values[index]=integer(v);
    }
    const auto [power,mode,units,target,sensor,frost]=values;
    if((power!=0 && power!=1) || !mode || *mode<0 || *mode>4)return {};
    Reading result{};
    result.power=*power==1;
    result.mode=static_cast<Mode>(*mode);
    if(units==0 && target && *target>=8 && *target<=30)result.setpoint_c=target;
    if(sensor && *sensor>=30 && *sensor<=90)result.indoor_c=*sensor-40;
    if(frost==0 || frost==1)result.frost=*frost==1;
    return result;
}

std::string command_request(Cipher cipher,const Key& key,std::string_view device,const Patch& patch) {
    if(!device_id(device) || (!patch.power && !patch.mode && !patch.setpoint_c) ||
       (patch.power==false && (patch.mode || patch.setpoint_c)) ||
       (patch.mode && static_cast<unsigned>(*patch.mode)>4) ||
       (patch.setpoint_c && (*patch.setpoint_c<16 || *patch.setpoint_c>30)))
        return {};
    std::string options,values;
    const auto add=[&](const char* name,int value) {
        if(!options.empty()) {options+=',';values+=',';}
        options+='"';options+=name;options+='"';
        values+=std::to_string(value);
    };
    if(patch.power)add("Pow",*patch.power?1:0);
    if(patch.mode)add("Mod",static_cast<int>(*patch.mode));
    if(patch.setpoint_c)add("SetTem",*patch.setpoint_c);
    return pack(cipher,key,false,"app",device,"{\"t\":\"cmd\",\"mac\":\""+
        std::string(device)+"\",\"opt\":["+options+"],\"p\":["+values+"]}");
}
bool heating_state_known(const Reading& reading) {
    return reading.frost==false && reading.setpoint_c.has_value();
}
PumpRoomMode heating_room_mode(const Reading& reading) {
    if(!heating_state_known(reading))return PumpRoomMode::Unknown;
    if(!reading.power)return PumpRoomMode::Off;
    return reading.mode==Mode::Heat?PumpRoomMode::Heat:PumpRoomMode::Other;
}
bool changes_operating_mode(const Patch& patch,const Reading& reading) {
    return (patch.power && *patch.power!=reading.power) ||
        (reading.power && patch.mode && *patch.mode!=reading.mode);
}
bool dwell_allows(const Patch& patch,const Reading& reading,uint32_t elapsed,uint32_t minimum) {
    return !changes_operating_mode(patch,reading) || elapsed>=minimum;
}
bool permits_heating_patch(const Patch& patch,const Reading& reading,const PumpHeatingLimits& limits) {
    const bool heating_target=!reading.power || reading.mode==Mode::Heat ||
        patch.mode==Mode::Heat;
    return limits.heating && heating_state_known(reading) &&
        (!patch.setpoint_c || (heating_target && limits.accepts(*patch.setpoint_c))) &&
        (!patch.mode || *patch.mode==Mode::Heat) &&
        (patch.power || patch.mode || patch.setpoint_c) &&
        !(patch.power==false && (patch.mode || patch.setpoint_c));
}
bool confirmed(const Patch& patch,const Reading& reading) {
    return (patch.power || patch.mode || patch.setpoint_c) &&
        (!patch.power || reading.power==*patch.power) &&
        (!patch.mode || reading.mode==*patch.mode) &&
        (!patch.setpoint_c || reading.setpoint_c==patch.setpoint_c);
}
}
