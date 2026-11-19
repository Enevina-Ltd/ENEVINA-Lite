#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "cottage/ota_recovery.hpp"
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
#include "cottage/ota_runtime_policy.hpp"

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
std::atomic<bool> pause_readers{false};
std::atomic<uint32_t> ota_written_bytes{0};
std::atomic<uint32_t> ota_completed_chunks{0};
std::atomic<uint32_t> ota_flash_yields{0};
// Dashboard requests schedule the existing signed worker. No HTTP upload and
// no direct flash write from WebServer callbacks.
std::atomic<TaskHandle_t> ota_worker_task{nullptr};
std::atomic<bool> ota_manual_pending{false};
std::atomic<uint32_t> ota_last_checked_utc{0};

namespace ota=enevina::cottage::ota;
// Compile-time check against the actual ESP-IDF headers in this firmware.
static_assert(ESP_OTA_IMG_NEW==0u && ESP_OTA_IMG_PENDING_VERIFY==1u &&
              ESP_OTA_IMG_VALID==2u && ESP_OTA_IMG_INVALID==3u &&
              ESP_OTA_IMG_ABORTED==4u &&
              static_cast<uint32_t>(ESP_OTA_IMG_UNDEFINED)==0xffffffffu,
              "ESP-IDF OTA states changed: update portable decoder");
std::atomic<bool> confirmation_pending{false};
bool read_transaction(Preferences& prefs,ota::Transaction& tx) {
    if(prefs.getBytesLength("txv2")!=sizeof(tx))return false;
    if(prefs.getBytes("txv2",&tx,sizeof(tx))!=sizeof(tx))return false;
    return ota::valid(tx);
}
bool write_transaction(Preferences& prefs,ota::Transaction tx) {
    ota::seal(tx);
    return prefs.putBytes("txv2",&tx,sizeof(tx))==sizeof(tx);
}
const esp_partition_t* candidate_partition(const ota::Transaction& tx) {
    const auto* p=esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        static_cast<esp_partition_subtype_t>(tx.target_subtype),nullptr);
    return p && p->address==tx.target_address?p:nullptr;
}
bool matching_image(const esp_partition_t* p,const ota::Transaction& tx) {
    uint8_t digest[32]{};
    return p && esp_partition_get_sha256(p,digest)==ESP_OK &&
           std::memcmp(digest,tx.target_image_sha256,32)==0;
}
ota::Evidence boot_evidence(const ota::Transaction& tx) {
    ota::Evidence e{};
    const auto* running=esp_ota_get_running_partition();
    const auto* boot=esp_ota_get_boot_partition();
    const auto* target=candidate_partition(tx);
    e.running_source=running && running->address==tx.source_address;
    e.running_target=running && running->address==tx.target_address &&
                     running->subtype==tx.target_subtype;
    e.selected_target=boot && boot->address==tx.target_address;
    e.target_hash_matches=matching_image(target,tx);
    esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
    if(target && esp_ota_get_state_partition(target,&state)==ESP_OK)
        e.target_state=ota::decode_esp_idf_image_state(static_cast<uint32_t>(state));
    return e;
}
struct NvsConfirm {
    Preferences& prefs;
    bool write_good(uint32_t v) {
        return prefs.putUInt("good",v)==sizeof(uint32_t);
    }
    bool remove_legacy_pending() {
        return !prefs.isKey("pending") || prefs.remove("pending");
    }
    bool remove_transaction() {
        return !prefs.isKey("txv2") || prefs.remove("txv2");
    }
};
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
bool update_app(const Release& release) {
    const esp_partition_t* active=esp_ota_get_running_partition();
    const esp_partition_t* target=esp_ota_get_next_update_partition(nullptr);
    if(!active || !target || target==active || release.size>target->size)return false;
    esp_ota_img_states_t old_target_state=ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(target,&old_target_state);
    auto client=open_https(COTTAGE_OTA_FIRMWARE_URL,static_cast<int>(release.size));
    if(!client)return false;
    esp_ota_handle_t handle=0;
    // A full image erase in esp_ota_begin() can block the IDLE watchdog
    // for seconds. Sequential erase bounds each flash operation to a sector.
    bool begun=esp_ota_begin(target,OTA_WITH_SEQUENTIAL_WRITES,&handle)==ESP_OK;
    if(!begun){close_https(client);return false;}
    unsigned char data[2048]{},digest[32]{};
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool okay=mbedtls_sha256_starts_ret(&sha,0)==0;
    uint32_t done=0;
    unsigned zero_reads=0;
    ota_written_bytes.store(0);
    ota_completed_chunks.store(0);
    ota_flash_yields.store(0);
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
        ota_written_bytes.store(done);
        ota_completed_chunks.fetch_add(1);
        // Yield to *lower-priority* IDLE tasks which service the task-WDT.
        // taskYIELD() only switches to ready tasks of equal/higher priority.
        // Do this after every bounded 2 KiB flash write, not just per 64 KiB.
        vTaskDelay(enevina::cottage::ota_flash_idle_delay_ticks);
        ota_flash_yields.fetch_add(1);
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
    ota::Transaction tx{};
    tx.version=release.version;
    tx.source_address=active->address;
    tx.target_address=target->address;
    tx.target_subtype=target->subtype;
    tx.previous_target_state=ota::decode_esp_idf_image_state(static_cast<uint32_t>(old_target_state));
    tx.phase=ota::Phase::Prepared;
    // The exact partition image hash, not just a version number, binds
    // recovery to this flashed candidate, even across power loss.
    if(esp_partition_get_sha256(target,tx.target_image_sha256)!=ESP_OK)
        return false;
    Preferences p;
    if(!p.begin("c24_ota",false))return false;
    if(p.isKey("txv2") || p.isKey("pending") ||
       !write_transaction(p,tx)) {
        p.end();return false;
    }
    p.end();
    if(esp_ota_set_boot_partition(target)!=ESP_OK) {
        // A failed activation is not a failed boot. If NVS cleanup itself
        // fails, initialize() will discard this Prepared intent next boot.
        Preferences cleanup;
        if(cleanup.begin("c24_ota",false)){
            cleanup.remove("txv2");cleanup.end();
        }
        return false;
    }
    // The bootloader's NEW record is durable now. A power cut before this
    // Armed update still safely boots the candidate; successful candidate
    // startup proves activation. Ambiguous failed boots are not quarantined.
    tx.phase=ota::Phase::Armed;
    Preferences armed;
    if(armed.begin("c24_ota",false)) {
        if(!write_transaction(armed,tx))
            Serial.println("C24_OTA_RECOVERY: activation armed marker deferred");
        armed.end();
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
    confirmation_pending.store(false);
    Preferences p;
    if(!p.begin("c24_ota",false)) {
        last_status.store(3);return;
    }
    const uint32_t good=p.getUInt("good",0);
    last_good.store(good);
    const uint32_t legacy=p.getUInt("pending",0);
    ota::Transaction tx{};
    bool has_tx=read_transaction(p,tx);
    if(p.isKey("txv2") && !has_tx) {
        // Corrupt/partial transaction cannot authorize an update or a
        // quarantine. Never erase it speculatively.
        last_status.store(3);p.end();return;
    }
    if(!has_tx && legacy) {
        if(good>=legacy) {
            // Crash between good write and legacy cleanup.
            if(!p.remove("pending"))last_status.store(3);
        } else {
            const auto* running=esp_ota_get_running_partition();
            const auto* old=esp_ota_get_next_update_partition(nullptr);
            esp_ota_img_states_t image_state=ESP_OTA_IMG_UNDEFINED;
            const bool candidate=running && old &&
                esp_ota_get_state_partition(running,&image_state)==ESP_OK &&
                image_state==ESP_OTA_IMG_PENDING_VERIFY;
            if(candidate) {
                // Pre-v2 signed releases persisted only pending/version.
                // Migrate ONLY a bootloader-proven first-boot candidate;
                // never infer identity from the NVS version alone.
                tx=ota::Transaction{};
                tx.version=legacy;
                tx.source_address=old->address;
                tx.target_address=running->address;
                tx.target_subtype=running->subtype;
                tx.phase=ota::Phase::Armed;
                if(esp_partition_get_sha256(running,tx.target_image_sha256)==ESP_OK &&
                   write_transaction(p,tx))has_tx=true;
                else last_status.store(3);
            } else {
                // An unproven legacy intent on the old VALID image may
                // be pre-activation; it is NOT evidence of a failed boot.
                if(!p.remove("pending"))last_status.store(3);
            }
        }
    }
    if(has_tx) {
        const ota::Evidence e=boot_evidence(tx);
        const auto action=ota::decide(tx,e);
        if(action==ota::Recovery::AwaitCandidateHealth) {
            confirmation_pending.store(true);
        } else if(action==ota::Recovery::QuarantineRealRollback) {
            // The bootloader, selected slot and SHA prove this attempt.
            // Never erase the recovery record before durable bad-version
            // quarantine has succeeded.
            const uint32_t bad=std::max(p.getUInt("bad",0),tx.version);
            if(p.putUInt("bad",bad)==sizeof(uint32_t) &&
               (!p.isKey("pending") || p.remove("pending")) &&
               p.remove("txv2")) {
                Serial.println("C24_OTA_RECOVERY: verified rollback quarantined");
            } else last_status.store(3);
        } else if(action==ota::Recovery::DiscardNeverActivated) {
            // A power cut during store_pending or set_boot_partition is
            // not a failed installation. Keep the signed version eligible.
            if(!p.remove("txv2") ||
               (p.isKey("pending") && !p.remove("pending")))
                last_status.store(3);
        } else if(action==ota::Recovery::Blocked) {
            // Unknown partition/image or ambiguous bootloader state:
            // neither confirm nor quarantine; block new OTA until known.
            last_status.store(3);
        }
        // KeepForActivation: do not force reboot or corrupt boot selection.
    }
    p.end();
}
bool cottage_ota_confirmation_pending() {
    return confirmation_pending.load();
}
bool cottage_ota_confirm_boot() {
    Preferences p;
    if(!p.begin("c24_ota",false))return false;
    ota::Transaction tx{};
    if(!read_transaction(p,tx)) {
        p.end();return false;
    }
    const auto e=boot_evidence(tx);
    // The bootloader VALID state is necessary but not sufficient:
    // commit only an identical flashed image running from the target.
    if(!e.running_target || !e.target_hash_matches ||
       e.target_state!=ota::ImageState::Valid) {
        p.end();return false;
    }
    const uint32_t good=p.getUInt("good",0);
    NvsConfirm store{p};
    const bool success=ota::confirm_persistence(store,tx,good);
    if(success) {
        last_good.store(std::max(good,tx.version));
        confirmation_pending.store(false);
    }
    p.end();
    return success;
}
uint32_t cottage_ota_good_version(){return last_good.load();}
uint32_t cottage_ota_last_candidate(){return last_candidate.load();}
int cottage_ota_status(){return last_status.load();}
bool cottage_ota_network_maintenance(){return pause_readers.load();}
uint32_t cottage_ota_downloaded_bytes(){return ota_written_bytes.load();}
uint32_t cottage_ota_chunk_count(){return ota_completed_chunks.load();}
uint32_t cottage_ota_idle_yields(){return ota_flash_yields.load();}
bool cottage_ota_request_immediate() {
    const auto task=ota_worker_task.load();
    if(!cottage_ota_configured() || cottage_ota_confirmation_pending() ||
       !task || pause_readers.load())return false;
    // Repeated clicks are idempotent until the worker begins the check.
    if(!ota_manual_pending.exchange(true))xTaskNotifyGive(task);
    return true;
}
bool cottage_ota_dashboard_request_pending(){return ota_manual_pending.load();}
uint32_t cottage_ota_last_check_utc(){return ota_last_checked_utc.load();}
void cottage_ota_worker(void*) {
#if !ENEVINA_SIGNED_OTA_AVAILABLE
    last_status.store(1);
    vTaskDelete(nullptr);
#else
    // This is deliberately an operator-staged, signed pull. No unauthenticated
    // device-side HTTP POST or firmware upload exists.
    ota_worker_task.store(xTaskGetCurrentTaskHandle());
    // Retain the boot stabilization interval; a queued dashboard request
    // survives it and is serviced as soon as the worker is ready.
    vTaskDelay(pdMS_TO_TICKS(90000));
    while(true) {
        ota_manual_pending.store(false);
        const time_t now=time(nullptr);
        if(now>1700000000)ota_last_checked_utc.store(static_cast<uint32_t>(now));
        // Keep normal CN105/Shelly readers running during routine manifest
        // checks. Briefly pause ONLY after the signed version is eligible.
        // Every exit path from an attempted update restores reader activity.
        cottage_ota_initialize();
        if(WiFi.status()!=WL_CONNECTED || time(nullptr)<1700000000) {
            last_status.store(2);
        } else if(!enevina_firmware::network_budget_acquire(200,
                  enevina::cottage::ota_manifest_min_internal_bytes,
                  enevina::cottage::ota_min_contiguous_internal_bytes)) {
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
                    // txv2 is authoritative for all new OTA releases.
                    if(p.isKey("txv2"))pending=1;
                    p.end();
                }
                if(!store_ready) {
                    last_status.store(3);
                } else if(!ota::signed_version_eligible(r.version,good,bad,pending!=0)) {
                    last_status.store(0);
                } else {
                    // The manifest TLS session has already been cleaned up.
                    // Release its shared network lock before allowing readers
                    // to drain: SSE may hold a separate, persistent socket.
                    enevina_firmware::network_budget_release();
                    pause_readers.store(true);
                    vTaskDelay(pdMS_TO_TICKS(
                        enevina::cottage::ota_readers_release_ms));
                    bool applied=false;
                    if(WiFi.status()!=WL_CONNECTED ||
                       !enevina_firmware::network_budget_acquire(1200,
                          enevina::cottage::ota_image_min_internal_bytes,
                          enevina::cottage::ota_min_contiguous_internal_bytes)) {
                        last_status.store(3);
                    } else {
                        last_status.store(5);
                        applied=update_app(r);
                        last_status.store(applied?6:4);
                        enevina_firmware::network_budget_release();
                    }
                    pause_readers.store(false);
                    if(applied) {
                        Serial.println("C24_SIGNED_OTA_VERIFIED: rebooting into standby partition");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    }
                    // A failed image stays non-authoritative. Keep current
                    // firmware and settings. Automatic retries retain the
                    // 10-minute cooldown; an explicit dashboard request can
                    // wake this worker once without bypassing signed checks.
                    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10*60*1000));
                    continue;
                }
            }
            enevina_firmware::network_budget_release();
        }
        // Routine 60 s checks remain unchanged, but the dashboard can
        // request an immediate pass without bypassing signature verification.
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(60000));
    }
#endif
}