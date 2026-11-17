#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <ctime>
#include "cottage_secure_ota.hpp"
#include "cottage_network_budget.hpp"

#if __has_include("cottage_ota_private.hpp")
#include "cottage_ota_private.hpp"
#define ENEVINA_SIGNED_OTA_AVAILABLE 1
#else
#define ENEVINA_SIGNED_OTA_AVAILABLE 0
#endif

#if ENEVINA_SIGNED_OTA_AVAILABLE
// Marker is retained in signed-pull-capable application images. The
// publisher refuses to sign an app image without this compiled capability.
// Volatile access prevents the compiler from eliding the ROM evidence.
static const volatile char signed_pull_marker[] =
    "ENEVINA_C24_SIGNED_PULL_OTA_CAPABLE_V1";
#ifndef COTTAGE_OTA_CHANNEL_MARKER
#error "Signed OTA requires a channel-bound generated cottage_ota_private.hpp"
#endif
// Retain the full public trust/endpoint fingerprint in the application image.
static const volatile char signed_channel_marker[] = COTTAGE_OTA_CHANNEL_MARKER;
#endif

namespace {
constexpr const char* magic="ENEVINA_C24_OTA_V1\nmodel=esp32s3-n16r8\n";
constexpr uint32_t max_image_size=0x640000u;
std::atomic<uint32_t> last_good{0}, last_candidate{0};
std::atomic<int> last_status{0}; // 0 idle; 1 disabled; 2 transient; 3 refused; 4 rejected; 5 downloading; 6 rebooting
struct Release {
    uint32_t version=0;
    uint32_t size=0;
    unsigned char digest[32]{};
};
bool hex_bytes(const char* ptr,size_t length,unsigned char* dest,size_t capacity) {
    if(length==0 || length%2 || length/2>capacity)return false;
    for(size_t i=0;i<length/2;i++) {
        auto from_hex=[](char c)->int {
            if(c>='0'&&c<='9')return c-'0';
            if(c>='a'&&c<='f')return c-'a'+10;
            return -1;
        };
        int a=from_hex(ptr[i*2]),b=from_hex(ptr[i*2+1]);
        if(a<0||b<0)return false;
        dest[i]=static_cast<unsigned char>((a<<4)|b);
    }
    return true;
}
#if ENEVINA_SIGNED_OTA_AVAILABLE
bool verify_manifest(char* data,size_t length,Release& out) {
    if(length<150 || length>=512 || data[length-1]!='\n') return false;
    if(std::memchr(data,0,length))return false;
    const char* sigline=std::strstr(data,"\nsig=");
    if(!sigline)return false;
    const size_t signedlen=static_cast<size_t>(sigline-data)+1;
    const char* signature_text=sigline+5;
    const size_t signature_length=length-(signature_text-data)-1;
    if(signature_length<110||signature_length>160||signature_text[signature_length]!='\n')return false;
    if(std::memcmp(data,magic,std::strlen(magic))!=0)return false;
    uint32_t version=0,size=0;
    char digesthex[65]{},tail='\0';
    // Parse ONLY the signed canonical prefix. Without this terminator,
    // the trailing signature line looks like an unexpected extra field.
    const char saved=data[signedlen];
    data[signedlen]='\0';
    const int parsed=std::sscanf(data+std::strlen(magic),
        "version=%u\nsize=%u\nsha256=%64[0-9a-f]\n%c",
        &version,&size,digesthex,&tail);
    data[signedlen]=saved;
    if(parsed!=3 || version==0 || version>2147483647 || size<1024 || size>max_image_size
       || std::strlen(digesthex)!=64)return false;
    char expected[220]{};
    const int rendered=std::snprintf(expected,sizeof(expected),
        "%sversion=%u\nsize=%u\nsha256=%s\n",
        magic,version,size,digesthex);
    if(rendered<0 || static_cast<size_t>(rendered)!=signedlen ||
       std::memcmp(expected,data,signedlen)!=0)return false;
    unsigned char signature[80]{};
    if(!hex_bytes(signature_text,signature_length,signature,sizeof(signature)))return false;
    unsigned char sha[32]{};
    if(mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(data),signedlen,sha,0)!=0)
        return false;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    const int key=mbedtls_pk_parse_public_key(&pk,
        reinterpret_cast<const unsigned char*>(COTTAGE_OTA_PUBLISHER_PUBLIC_KEY),
        std::strlen(COTTAGE_OTA_PUBLISHER_PUBLIC_KEY)+1);
    const int sig=(key==0)?mbedtls_pk_verify(&pk,MBEDTLS_MD_SHA256,
                sha,sizeof(sha),signature,signature_length/2):-1;
    mbedtls_pk_free(&pk);
    if(sig!=0)return false;
    out.version=version;out.size=size;
    return hex_bytes(digesthex,64,out.digest,sizeof(out.digest));
}

esp_http_client_handle_t open_https(const char* url,int expected_length) {
    esp_http_client_config_t cfg{};
    cfg.url=url;
    cfg.cert_pem=COTTAGE_OTA_LOCAL_ROOT_CA;
    cfg.timeout_ms=8500;
    cfg.disable_auto_redirect=true;
    cfg.buffer_size=1024;
    cfg.buffer_size_tx=512;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client)return nullptr;
    if(esp_http_client_open(client,0)!=ESP_OK) {
        esp_http_client_cleanup(client);
        return nullptr;
    }
    const int length=esp_http_client_fetch_headers(client);
    if(esp_http_client_get_status_code(client)!=200 || length!=expected_length) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return nullptr;
    }
    return client;
}
void close_https(esp_http_client_handle_t client) {
    if(client){esp_http_client_close(client);esp_http_client_cleanup(client);}
}
bool fetch_manifest(Release& out) {
    // HTTP status/content-length and TLS certificate must all validate.
    esp_http_client_config_t cfg{};
    cfg.url=COTTAGE_OTA_MANIFEST_URL;
    cfg.cert_pem=COTTAGE_OTA_LOCAL_ROOT_CA;
    cfg.timeout_ms=8500;
    cfg.disable_auto_redirect=true;
    cfg.buffer_size=1024;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client)return false;
    bool success=false;
    if(esp_http_client_open(client,0)==ESP_OK) {
        const int len=esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client)==200 && len>145 && len<512) {
            char body[512]{};
            int n=0,waits=0;
            while(n<len && waits<12) {
                const int read=esp_http_client_read(client,body+n,len-n);
                if(read<0)break;
                if(read==0){waits++;vTaskDelay(pdMS_TO_TICKS(50));continue;}
                n+=read;
            }
            if(n==len)success=verify_manifest(body,n,out);
        }
    }
    close_https(client);
    return success;
}
bool store_pending(uint32_t version) {
    Preferences p;
    if(!p.begin("c24_ota",false))return false;
    const bool ok=p.putUInt("pending",version)==sizeof(uint32_t);
    p.end();
    return ok;
}
bool update_app(const Release& release) {
    const esp_partition_t* active=esp_ota_get_running_partition();
    const esp_partition_t* target=esp_ota_get_next_update_partition(nullptr);
    if(!active || !target || target==active || release.size>target->size)return false;
    auto client=open_https(COTTAGE_OTA_FIRMWARE_URL,static_cast<int>(release.size));
    if(!client)return false;
    esp_ota_handle_t handle=0;
    bool begun=esp_ota_begin(target,release.size,&handle)==ESP_OK;
    if(!begun){close_https(client);return false;}
    unsigned char data[2048]{},digest[32]{};
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool okay=mbedtls_sha256_starts_ret(&sha,0)==0;
    uint32_t done=0;
    unsigned zero_reads=0;
    while(okay && done<release.size) {
        if(WiFi.status()!=WL_CONNECTED ||
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<24*1024) {
            okay=false;break;
        }
        const int chunk=static_cast<int>(std::min<uint32_t>(sizeof(data),release.size-done));
        const int got=esp_http_client_read(client,reinterpret_cast<char*>(data),chunk);
        if(got<0){okay=false;break;}
        if(got==0) {
            if(++zero_reads>25){okay=false;break;}
            vTaskDelay(pdMS_TO_TICKS(30));continue;
        }
        zero_reads=0;
        if(mbedtls_sha256_update_ret(&sha,data,got)!=0 ||
           esp_ota_write(handle,data,got)!=ESP_OK){okay=false;break;}
        done+=got;
        taskYIELD();
    }
    if(okay && mbedtls_sha256_finish_ret(&sha,digest)!=0)okay=false;
    mbedtls_sha256_free(&sha);
    close_https(client);
    if(!okay || done!=release.size ||
       std::memcmp(digest,release.digest,32)!=0) {
        esp_ota_abort(handle);
        return false;
    }
    if(esp_ota_end(handle)!=ESP_OK)return false; // app image verified by IDF
    if(!store_pending(release.version))return false;
    if(esp_ota_set_boot_partition(target)!=ESP_OK) {
        Preferences p;
        if(p.begin("c24_ota",false)){p.remove("pending");p.end();}
        return false;
    }
    return true;
}
#endif // ENABLED
}
bool cottage_ota_configured(){
#if ENEVINA_SIGNED_OTA_AVAILABLE
    return signed_pull_marker[0]=='E' && signed_channel_marker[0]=='E';
#else
    return false;
#endif
}
void cottage_ota_initialize() {
    Preferences p;
    if(!p.begin("c24_ota",false))return;
    const auto good=p.getUInt("good",0);
    last_good.store(good);
    const auto pending=p.getUInt("pending",0);
    if(pending) {
        const esp_partition_t* running=esp_ota_get_running_partition();
        esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
        const bool still_pending=running &&
            esp_ota_get_state_partition(running,&state)==ESP_OK &&
            state==ESP_OTA_IMG_PENDING_VERIFY;
        if(!still_pending) {
            // Old valid image booted after a failed candidate: quarantine it.
            p.putUInt("bad",std::max(p.getUInt("bad",0),pending));
            p.remove("pending");
        }
    }
    p.end();
}
void cottage_ota_confirm_boot() {
    Preferences p;
    if(!p.begin("c24_ota",false))return;
    const auto pending=p.getUInt("pending",0);
    if(pending) {
        const auto good=std::max(p.getUInt("good",0),pending);
        if(p.putUInt("good",good)==sizeof(uint32_t)) {
            p.remove("pending");last_good.store(good);
        }
    }
    p.end();
}
uint32_t cottage_ota_good_version(){return last_good.load();}
uint32_t cottage_ota_last_candidate(){return last_candidate.load();}
int cottage_ota_status(){return last_status.load();}
void cottage_ota_worker(void*) {
#if !ENEVINA_SIGNED_OTA_AVAILABLE
    last_status.store(1);
    vTaskDelete(nullptr);
#else
    // This is deliberately an operator-staged, signed pull. No unauthenticated
    // device-side HTTP POST or firmware upload exists. Publisher login to the
    // LAN host + signing key access is the approval step.
    vTaskDelay(pdMS_TO_TICKS(90000));
    while(true) {
        if(WiFi.status()!=WL_CONNECTED || time(nullptr)<1700000000) {
            last_status.store(2);
        } else if(!enevina_firmware::network_budget_acquire(200,
                  80*1024,20*1024)) {
            last_status.store(3);
        } else {
            Release r{};
            const bool valid=fetch_manifest(r);
            if(!valid) {
                last_status.store(4);
            } else {
                last_candidate.store(r.version);
                Preferences p;
                uint32_t good=0,bad=0,pending=0;
                const bool store_ready=p.begin("c24_ota",true);
                if(store_ready) {
                    good=p.getUInt("good",0);
                    bad=p.getUInt("bad",0);
                    pending=p.getUInt("pending",0);
                    p.end();
                }
                if(!store_ready) {
                    // Refuse all OTA if persistent anti-rollback state
                    // cannot be read; never treat NVS failure as version zero.
                    last_status.store(3);
                } else if(r.version<=good || r.version<=bad || pending) {
                    last_status.store(0);
                } else {
                    last_status.store(5);
                    const bool applied=update_app(r);
                    last_status.store(applied?6:4);
                    enevina_firmware::network_budget_release();
                    if(applied) {
                        Serial.println("C24_SIGNED_OTA_VERIFIED: rebooting into standby partition");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    }
                    vTaskDelay(pdMS_TO_TICKS(10*60*1000));
                    continue;
                }
            }
            enevina_firmware::network_budget_release();
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
#endif
}
