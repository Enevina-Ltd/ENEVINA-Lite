#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <nvs.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "cottage/ota_recovery.hpp"
#include <esp_heap_caps.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <strings.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <memory>
#include <ctime>
#include "cottage_secure_ota.hpp"
#include "cottage_history.hpp"
#include "cottage_network_budget.hpp"
#include "cottage_tls_memory.hpp"
#include "cottage/ota_runtime_policy.hpp"
#include "cottage/ota_history_policy.hpp"
#include "cottage/ota_image_staging.hpp"

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
std::atomic<uint32_t> last_history_upgrade{0},last_history_ack{0};
std::atomic<uint32_t> last_history_version{0},last_history_utc{0};
constexpr char kHistoryUpgradeKey[]="hist_upg";
constexpr char kHistoryAckKey[]="hist_done";
constexpr char kHistoryLastKey[]="hist_last";
std::atomic<int> last_status{0}; // 0 idle; 1 disabled; 2 transient; 3 refused; 4 rejected; 5 downloading; 6 rebooting
// Status 3 used to conflate shared-mutex, low heap and NVS refusals.
// Keep phase-specific evidence captured at the actual denial, under the
// network budget mutex; sampling heap later would misidentify the cause.
enum class OtaProblemStage : uint8_t {None=0, ManifestLease=1, Storage=2,
                                     ImageLease=3, Transport=4, Flash=5};
enum class OtaProblemReason : uint8_t {None=0, MutexBusy=1, LowMemory=2,
                                      NvsUnavailable=3, WifiLost=4,
                                      ImageFailed=5, HttpOpen=6,
                                      FlashBegin=7, WifiDuringImage=8,
                                      InflightLowMemory=9, HttpReadError=10,
                                      HttpReadTimeout=11, ShaFailed=12,
                                      FlashWriteFailed=13, WrongLength=14,
                                      DigestMismatch=15, FlashEndFailed=16,
                                      ImageShaFailed=17, NvsWriteFailed=18,
                                      BootSelectFailed=19, PsramUnavailable=20, TransferDeadline=21, RetryRequiresRequest=22, RangeMismatch=23, HistoryFlushFailed=24, LearningFlushFailed=25};
std::atomic<uint8_t> ota_problem_stage{0}, ota_problem_reason{0};
std::atomic<uint32_t> ota_problem_free{0}, ota_problem_largest{0};
std::atomic<uint32_t> ota_image_attempts{0};
std::atomic<uint32_t> ota_inflight_memory_waits{0};
void ota_record_problem(OtaProblemStage stage,OtaProblemReason reason,
                        const enevina_firmware::NetworkBudgetDiagnostic& diag={}) {
    ota_problem_free.store(diag.free_internal);
    ota_problem_largest.store(diag.largest_internal);
    ota_problem_reason.store(static_cast<uint8_t>(reason));
    ota_problem_stage.store(static_cast<uint8_t>(stage));
}
void ota_reset_problem() {
    ota_problem_stage.store(0);ota_problem_reason.store(0);
    ota_problem_free.store(0);ota_problem_largest.store(0);
    ota_image_attempts.store(0);
    ota_inflight_memory_waits.store(0);
}
OtaProblemReason ota_budget_reason(enevina_firmware::NetworkBudgetFailure failure) {
    return failure==enevina_firmware::NetworkBudgetFailure::MutexBusy?
        OtaProblemReason::MutexBusy:OtaProblemReason::LowMemory;
}
std::atomic<bool> pause_readers{false};
std::atomic<uint32_t> ota_written_bytes{0}; // downloaded into PSRAM
std::atomic<uint32_t> ota_programmed_bytes{0};
std::atomic<uint32_t> ota_expected_bytes{0}, ota_resume_count{0}, ota_durable_bytes{0};
std::atomic<bool> ota_reconnecting{false};
std::atomic<uint32_t> ota_completed_chunks{0};
std::atomic<uint32_t> ota_flash_yields{0};
// Dashboard requests schedule the existing signed worker. No HTTP upload and
// no direct flash write from WebServer callbacks.
std::atomic<TaskHandle_t> ota_worker_task{nullptr};
std::atomic<bool> ota_manual_pending{false};
std::atomic<uint32_t> ota_last_checked_utc{0};

namespace ota=enevina::cottage::ota;
#if ENEVINA_SIGNED_OTA_AVAILABLE
constexpr char kAttemptedVersionKey[]="attempted";
constexpr char kRetryIdentityKey[]="retry_v1";
bool read_retry_identity(nvs_handle_t h,ota::SpoolIdentity& id) {
    size_t n=sizeof(id);
    return nvs_get_blob(h,kRetryIdentityKey,&id,&n)==ESP_OK && n==sizeof(id) && ota::spool_valid(id);
}
bool retry_identity_matches(uint32_t attempted,const ota::SpoolIdentity& wanted) {
    nvs_handle_t h;
    if(nvs_open("c24_ota",NVS_READONLY,&h)!=ESP_OK)return false;
    ota::SpoolIdentity saved{};
    const bool match=read_retry_identity(h,saved)&&ota::spool_retry_allowed(attempted,saved,wanted);
    nvs_close(h);return match;
}
bool clear_retry_identity() {
    nvs_handle_t h;
    if(nvs_open("c24_ota",NVS_READWRITE,&h)!=ESP_OK)return false;
    const esp_err_t e=nvs_erase_key(h,kRetryIdentityKey);
    size_t size=0;
    const bool ok=(e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND)&&nvs_commit(h)==ESP_OK &&
        nvs_get_blob(h,kRetryIdentityKey,nullptr,&size)==ESP_ERR_NVS_NOT_FOUND;
    nvs_close(h);return ok;
}
bool read_attempted_version(uint32_t& attempted) {
    nvs_handle_t h;
    if(nvs_open("c24_ota",NVS_READONLY,&h)!=ESP_OK)return false;
    attempted=0;
    const esp_err_t err=nvs_get_u32(h,kAttemptedVersionKey,&attempted);
    nvs_close(h);
    return err==ESP_OK || err==ESP_ERR_NVS_NOT_FOUND;
}
bool remember_image_attempt(const ota::SpoolIdentity& identity,bool explicit_request) {
    const uint32_t version=identity.version;
    nvs_handle_t h;
    if(nvs_open("c24_ota",NVS_READWRITE,&h)!=ESP_OK)return false;
    uint32_t previous=0;
    const esp_err_t err=nvs_get_u32(h,kAttemptedVersionKey,&previous);
    bool ok=(err==ESP_OK || err==ESP_ERR_NVS_NOT_FOUND) &&
        ota::image_attempt_allowed(version,previous,explicit_request);
    if(ok) {
        // Persist the exact authenticated transfer intent before the legacy
        // attempted high-water mark. A reset at either write may re-fetch
        // this identity, but not auto-retry a different rejected release.
        ota::SpoolIdentity saved{};
        if(!read_retry_identity(h,saved)||!ota::spool_matches(saved,identity))
            ok=nvs_set_blob(h,kRetryIdentityKey,&identity,sizeof(identity))==ESP_OK && nvs_commit(h)==ESP_OK;
        if(ok && version>previous)
            ok=nvs_set_u32(h,kAttemptedVersionKey,version)==ESP_OK && nvs_commit(h)==ESP_OK;
        uint32_t durable=0;
        ok=ok && nvs_get_u32(h,kAttemptedVersionKey,&durable)==ESP_OK && durable>=version &&
            read_retry_identity(h,saved)&&ota::spool_matches(saved,identity);
    }
    nvs_close(h);
    return ok;
}
#endif
// Compile-time check against the actual ESP-IDF headers in this firmware.
static_assert(ESP_OTA_IMG_NEW==0u && ESP_OTA_IMG_PENDING_VERIFY==1u &&
              ESP_OTA_IMG_VALID==2u && ESP_OTA_IMG_INVALID==3u &&
              ESP_OTA_IMG_ABORTED==4u &&
              static_cast<uint32_t>(ESP_OTA_IMG_UNDEFINED)==0xffffffffu,
              "ESP-IDF OTA states changed: update portable decoder");
std::atomic<bool> confirmation_pending{false};
std::atomic<bool> ota_storage_ready{false};
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

// Exact ETag and Content-Range are required for every bounded range. TLS
// authenticates the peer; the signed manifest pins the whole image digest.
struct RangeHeaders {
    char etag[67]{}, range[80]{};
    bool invalid=false;
};
esp_err_t range_header_event(esp_http_client_event_t* event) {
    if(event->event_id!=HTTP_EVENT_ON_HEADER)return ESP_OK;
    auto& h=*static_cast<RangeHeaders*>(event->user_data);
    const char* key=event->header_key;
    const char* value=event->header_value;
    if(!key||!value){h.invalid=true;return ESP_OK;}
    auto capture=[&](char* dest,size_t cap) {
        const size_t n=std::strlen(value);
        if(dest[0]||n>=cap)h.invalid=true;
        else std::memcpy(dest,value,n+1);
    };
    if(strcasecmp(key,"ETag")==0)capture(h.etag,sizeof(h.etag));
    if(strcasecmp(key,"Content-Range")==0)capture(h.range,sizeof(h.range));
    if(strcasecmp(key,"Content-Encoding")==0 && strcasecmp(value,"identity")!=0)
        h.invalid=true;
    return ESP_OK;
}
void close_https(esp_http_client_handle_t client) {
    if(client){esp_http_client_close(client);esp_http_client_cleanup(client);}
}
enum class ManifestResult {Valid,Unavailable,NotOffered,Rejected};
ManifestResult fetch_manifest(Release& out) {
    enevina_firmware::PublicTlsMemoryScope tls_memory(enevina_firmware::PublicTlsSource::Ota);
    // HTTP status/content-length and TLS certificate must all validate.
    esp_http_client_config_t cfg{};
    cfg.url=COTTAGE_OTA_MANIFEST_URL;
    cfg.cert_pem=COTTAGE_OTA_LOCAL_ROOT_CA;
    cfg.timeout_ms=8500;
    cfg.disable_auto_redirect=true;
    cfg.buffer_size=1024;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client)return ManifestResult::Unavailable;
    ManifestResult result=ManifestResult::Unavailable;
    if(esp_http_client_open(client,0)==ESP_OK) {
        const int len=esp_http_client_fetch_headers(client);
        const int status=esp_http_client_get_status_code(client);
        if(status==200 && len>145 && len<512) {
            char body[512]{};
            int n=0,waits=0;
            while(n<len && waits<12) {
                const int read=esp_http_client_read(client,body+n,len-n);
                if(read<0)break;
                if(read==0){waits++;vTaskDelay(pdMS_TO_TICKS(50));continue;}
                n+=read;
            }
            if(n==len)result=verify_manifest(body,n,out)?ManifestResult::Valid:ManifestResult::Rejected;
        } else if(status==404)result=ManifestResult::NotOffered;
        else if(status==200 && len>=0)result=ManifestResult::Rejected;
        else if(status>=400 && status<500 && status!=408 && status!=429)
            result=ManifestResult::Rejected;
    }
    close_https(client);
    return result;
}
// The OTA worker stack is internal RAM. Never pass a PSRAM pointer to a
// flash driver that may disable the external-memory cache during writes.
bool ota_transfer_ready(OtaProblemStage stage) {
    if(stage==OtaProblemStage::Transport && WiFi.status()!=WL_CONNECTED) {
        ota_record_problem(stage,OtaProblemReason::WifiDuringImage);
        return false;
    }
    uint32_t internal_free=static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    for(unsigned wait=0;
        internal_free<enevina::cottage::ota_inflight_min_internal_bytes &&
        wait<enevina::cottage::ota_inflight_recheck_max_waits;
        ++wait) {
        ota_inflight_memory_waits.fetch_add(1);
        vTaskDelay(pdMS_TO_TICKS(enevina::cottage::ota_inflight_recheck_delay_ms));
        internal_free=static_cast<uint32_t>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    }
    if(internal_free<enevina::cottage::ota_inflight_min_internal_bytes) {
        enevina_firmware::NetworkBudgetDiagnostic actual{};
        actual.free_internal=internal_free;
        actual.largest_internal=static_cast<uint32_t>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        ota_record_problem(stage,OtaProblemReason::InflightLowMemory,actual);
        return false;
    }
    if(stage==OtaProblemStage::Transport && WiFi.status()!=WL_CONNECTED) {
        ota_record_problem(stage,OtaProblemReason::WifiDuringImage);
        return false;
    }
    return true;
}
enum class RangeResult {Advanced,Retry,Reject};
using OtaImage=ota::SegmentedImage<enevina::cottage::ota_range_bytes,
    max_image_size/enevina::cottage::ota_range_bytes>;
void* alloc_ota_psram(size_t n){return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
RangeResult download_range(const Release& release,OtaImage& image,uint8_t* data,
                           size_t capacity,uint32_t& done,uint32_t& last_progress) {
    using namespace enevina::cottage;
    // Reserve TLS memory only for one bounded range. Other network users
    // get a scheduling window before the next connection.
    enevina_firmware::NetworkBudgetLease lease(1200,ota_image_min_internal_bytes,
        ota_min_contiguous_internal_bytes);
    ota_image_attempts.fetch_add(1);
    if(!lease.held()) {
        ota_record_problem(OtaProblemStage::ImageLease,
            ota_budget_reason(lease.diagnostic().failure),lease.diagnostic());
        return RangeResult::Retry;
    }
    if(!ota_transfer_ready(OtaProblemStage::Transport))return RangeResult::Retry;
    const uint32_t lease_started=millis();
    const uint32_t begin=done;
    const uint32_t end=begin+std::min(ota_range_bytes,release.size-begin)-1;
    char etag[67]{};
    etag[0]='"';
    for(unsigned i=0;i<32;++i)
        std::snprintf(etag+1+2*i,3,"%02x",static_cast<unsigned>(release.digest[i]));
    etag[65]='"';etag[66]=0;
    char range[64]{},expected[80]{};
    std::snprintf(range,sizeof(range),"bytes=%lu-%lu",
        static_cast<unsigned long>(begin),static_cast<unsigned long>(end));
    std::snprintf(expected,sizeof(expected),"bytes %lu-%lu/%lu",
        static_cast<unsigned long>(begin),static_cast<unsigned long>(end),
        static_cast<unsigned long>(release.size));
    RangeHeaders headers{};
    esp_http_client_config_t cfg{};
    cfg.url=COTTAGE_OTA_FIRMWARE_URL;cfg.cert_pem=COTTAGE_OTA_LOCAL_ROOT_CA;
    cfg.timeout_ms=12000;cfg.disable_auto_redirect=true;
    cfg.buffer_size=1024;cfg.buffer_size_tx=512;
    cfg.event_handler=range_header_event;cfg.user_data=&headers;
    enevina_firmware::PublicTlsMemoryScope tls_memory(enevina_firmware::PublicTlsSource::Ota);
    auto client=esp_http_client_init(&cfg);
    if(!client)return RangeResult::Retry;
    struct Close {esp_http_client_handle_t client;~Close(){close_https(client);}} closer{client};
    if(esp_http_client_set_header(client,"Range",range)!=ESP_OK ||
       esp_http_client_set_header(client,"If-Match",etag)!=ESP_OK ||
       esp_http_client_set_header(client,"Accept-Encoding","identity")!=ESP_OK ||
       esp_http_client_open(client,0)!=ESP_OK) {
        ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::HttpOpen);
        return RangeResult::Retry;
    }
    const int64_t length=esp_http_client_fetch_headers(client);
    const int status=esp_http_client_get_status_code(client);
    if(length<0 || status>=500 || status==0 || status==404 || status==408 || status==429) {
        ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::HttpOpen);
        return RangeResult::Retry;
    }
    if(!ota::range_response_matches(status,headers.invalid,headers.etag,etag,
       headers.range,expected,length,end-begin+1) ||
       esp_http_client_is_chunked_response(client)) {
        ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::RangeMismatch);
        return RangeResult::Reject;
    }
    while(done<=end) {
        // A trickling peer must not monopolize the TLS lease. The next
        // Range starts at done, including a partially consumed response.
        if(done>begin && static_cast<uint32_t>(millis()-lease_started)>=ota_range_lease_slice_ms)
            return RangeResult::Advanced;
        if(ota_progress_expired(millis(),last_progress) ||
           !ota_transfer_ready(OtaProblemStage::Transport))return RangeResult::Retry;
        const size_t want=std::min<size_t>(capacity,end-done+1);
        const int got=esp_http_client_read(client,reinterpret_cast<char*>(data),want);
        if(got<=0) {
            ota_record_problem(OtaProblemStage::Transport,got<0?
                OtaProblemReason::HttpReadError:OtaProblemReason::HttpReadTimeout);
            return RangeResult::Retry;
        }
        if(static_cast<size_t>(got)>want) {
            ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::WrongLength);
            return RangeResult::Reject;
        }
        // Header identity is pinned, but no byte is trusted for flashing
        // until the full signed SHA-256 matches. Retain even partial ranges.
        if(!image.write(done,data,static_cast<size_t>(got))) {
            ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::WrongLength);
            return RangeResult::Reject;
        }
        done+=static_cast<uint32_t>(got);
        last_progress=millis();
        ota_written_bytes.store(static_cast<uint32_t>(done));
        ota_reconnecting.store(false);
        vTaskDelay(1);
    }
    if(!esp_http_client_is_complete_data_received(client)) {
        ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::WrongLength);
        return RangeResult::Reject;
    }
    return RangeResult::Advanced;
}
bool retryable_image_failure() {
    switch(static_cast<OtaProblemReason>(ota_problem_reason.load())) {
        case OtaProblemReason::MutexBusy:
        case OtaProblemReason::LowMemory:
        case OtaProblemReason::WifiLost:
        case OtaProblemReason::HttpOpen:
        case OtaProblemReason::WifiDuringImage:
        case OtaProblemReason::InflightLowMemory:
        case OtaProblemReason::HttpReadError:
        case OtaProblemReason::HttpReadTimeout:
        case OtaProblemReason::PsramUnavailable:
        case OtaProblemReason::TransferDeadline:
            return true;
        default:return false; // integrity, flash and activation failures require a new request
    }
}
bool update_app(const Release& release) {
    ota_written_bytes.store(0);
    ota_expected_bytes.store(release.size);
    ota_resume_count.store(0);
    ota_durable_bytes.store(0);
    ota_reconnecting.store(false);
    ota_programmed_bytes.store(0);
    ota_completed_chunks.store(0);
    ota_flash_yields.store(0);
    const esp_partition_t* active=esp_ota_get_running_partition();
    const esp_partition_t* target=esp_ota_get_next_update_partition(nullptr);
    if(!active || !target || target==active || release.size>target->size) {
        ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::FlashBegin);
        return false;
    }
    esp_ota_img_states_t old_target_state=ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(target,&old_target_state);

    // No internal-heap fallback: if external RAM cannot hold this exact
    // signed image plus the reserved headroom, leave both app slots intact.
    const uint32_t psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(psram<release.size || psram-release.size<enevina::cottage::ota_staging_psram_reserve_bytes) {
        ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::PsramUnavailable);
        return false;
    }
    OtaImage image(alloc_ota_psram,heap_caps_free);
    if(!image.reserve(release.size)) {
        ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::PsramUnavailable);
        return false;
    }
    uint8_t data[enevina::cottage::ota_flash_chunk_bytes]{};
    const auto identity=ota::spool_identity(release.version,release.size,release.digest);
    static_assert(ota::spool_block_bytes==enevina::cottage::ota_range_bytes,"stage block mismatch");
    uint32_t received=0,committed=0;
    bool spool=enevina_firmware::history_ota_spool_prepare(identity);
    if(spool) {
        // Scan only a contiguous, CRC-verified prefix belonging to this
        // signed identity. A torn or corrupt block is downloaded again.
        while(received<release.size) {
            size_t n=ota::spool_payload(identity,received);
            auto* span=image.span(received,n);
            if(!span||!n||!enevina_firmware::history_ota_spool_read(identity,received,span))break;
            received+=static_cast<uint32_t>(n);
            ota_durable_bytes.store(received);ota_written_bytes.store(received);
            vTaskDelay(1);
        }
        committed=received;
    }
    uint32_t last_progress=millis();
    while(received<release.size) {
        if(enevina::cottage::ota_progress_expired(millis(),last_progress)) {
            ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::TransferDeadline);
            ota_reconnecting.store(false);
            return false;
        }
        const auto result=download_range(release,image,data,sizeof(data),received,last_progress);
        if(result==RangeResult::Reject) {ota_reconnecting.store(false);return false;}
        // TLS is closed and the transfer lease released here. Persist only
        // complete blocks, before advancing the durable byte count.
        while(spool && committed<received) {
            size_t n=ota::spool_payload(identity,committed);
            auto* span=image.span(committed,n);
            if(!span||!n||n>received-committed)break;
            if(!enevina_firmware::history_ota_spool_store(identity,committed,span)) {
                spool=false;break; // never take capacity from history
            }
            committed+=static_cast<uint32_t>(n);ota_durable_bytes.store(committed);
            vTaskDelay(1);
        }
        if(result==RangeResult::Retry) {
            ota_reconnecting.store(true);ota_resume_count.fetch_add(1);
            vTaskDelay(pdMS_TO_TICKS(enevina::cottage::ota_retry_delay_ms));
        } else {
            ota_problem_stage.store(0);ota_problem_reason.store(0);
            vTaskDelay(pdMS_TO_TICKS(enevina::cottage::ota_reader_window_ms));
        }
    }
    ota_reconnecting.store(false);
    uint8_t digest[32]{};
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    int hash_result=mbedtls_sha256_starts_ret(&sha,0);
    for(uint32_t pos=0;hash_result==0 && pos<release.size;) {
        size_t n=release.size-pos;
        const auto* span=image.span(pos,n);
        if(!span||!n){hash_result=-1;break;}
        hash_result=mbedtls_sha256_update_ret(&sha,span,n);
        pos+=static_cast<uint32_t>(n);vTaskDelay(1);
    }
    if(hash_result==0)hash_result=mbedtls_sha256_finish_ret(&sha,digest);
    mbedtls_sha256_free(&sha);
    if(hash_result!=0) {
        ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::ShaFailed);return false;
    }
    if(std::memcmp(digest,release.digest,32)!=0) {
        ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::DigestMismatch);return false;
    }
    // Only installation needs maintenance. TLS is closed before flash.
    // Exclude competing TLS/BLE allocations while the flash driver runs;
    // Wi-Fi loss cannot invalidate a fully authenticated staged image.
    pause_readers.store(true);
    vTaskDelay(pdMS_TO_TICKS(enevina::cottage::ota_readers_release_ms));
    { // Only flash I/O owns the shared network-memory lease.
    enevina_firmware::NetworkBudgetLease flash_lease(5000,
        enevina::cottage::ota_image_min_internal_bytes,
        enevina::cottage::ota_min_contiguous_internal_bytes);
    if(!flash_lease.held()) {
        ota_record_problem(OtaProblemStage::Flash,
            ota_budget_reason(flash_lease.diagnostic().failure),flash_lease.diagnostic());
        return false;
    }
    if(!ota_transfer_ready(OtaProblemStage::Flash))return false;
    esp_ota_handle_t handle=0;
    if(esp_ota_begin(target,OTA_WITH_SEQUENTIAL_WRITES,&handle)!=ESP_OK) {
        ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::FlashBegin);
        return false;
    }
    uint32_t done=0;
    while(done<release.size) {
        if(!ota_transfer_ready(OtaProblemStage::Flash)) {
            esp_ota_abort(handle);
            return false;
        }
        const uint32_t chunk=std::min<uint32_t>(sizeof(data),release.size-done);
        if(!image.read(done,data,chunk)) {
            ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::WrongLength);
            esp_ota_abort(handle);return false;
        }
        if(esp_ota_write(handle,data,chunk)!=ESP_OK) {
            ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::FlashWriteFailed);
            esp_ota_abort(handle);
            return false;
        }
        done+=chunk;
        ota_programmed_bytes.store(done);
        ota_completed_chunks.fetch_add(1);
        vTaskDelay(enevina::cottage::ota_flash_idle_delay_ticks);
        ota_flash_yields.fetch_add(1);
    }
    image.reset();
    if(!ota_transfer_ready(OtaProblemStage::Flash)) {
        esp_ota_abort(handle);
        return false;
    }
    if(esp_ota_end(handle)!=ESP_OK){
        ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::FlashEndFailed);
        return false;
    } // app image verified by IDF
    } // Release before asking the optimizer to checkpoint its state.
    ota::Transaction tx{};
    tx.version=release.version;
    tx.source_address=active->address;
    tx.target_address=target->address;
    tx.target_subtype=target->subtype;
    tx.previous_target_state=ota::decode_esp_idf_image_state(static_cast<uint32_t>(old_target_state));
    tx.phase=ota::Phase::Prepared;
    // The exact partition image hash, not just a version number, binds
    // recovery to this flashed candidate, even across power loss.
    if(esp_partition_get_sha256(target,tx.target_image_sha256)!=ESP_OK){
        ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::ImageShaFailed);
        return false;
    }
#ifndef COTTAGE_BENCH_MODE
    if(!cottage_ota_checkpoint_learning()) {
        ota_record_problem(OtaProblemStage::Storage,OtaProblemReason::LearningFlushFailed);
        return false;
    }
#endif
    if(!enevina_firmware::history_flush_for_ota()) {
        ota_record_problem(OtaProblemStage::Storage,OtaProblemReason::HistoryFlushFailed);
        return false;
    }
    Preferences p;
    if(!p.begin("c24_ota",false)){
        ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::NvsWriteFailed);
        return false;
    }
    if(p.isKey("txv2") || p.isKey("pending") ||
       !write_transaction(p,tx)) {
        ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::NvsWriteFailed);
        p.end();return false;
    }
    p.end();
    if(esp_ota_set_boot_partition(target)!=ESP_OK) {
        ota_record_problem(OtaProblemStage::Flash,OtaProblemReason::BootSelectFailed);
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
    enevina_firmware::history_ota_spool_discard();
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
    ota_storage_ready.store(false);
    confirmation_pending.store(false);
    Preferences p;
    if(!p.begin("c24_ota",false)) {
        last_status.store(3);return;
    }
    const uint32_t good=p.getUInt("good",0);
    last_good.store(good);
    // Unlike the volatile HistoryRecorder baseline, this record survives
    // reboot after confirmation. It never proves success by itself: the
    // signed-OTA good version must already have been durably confirmed.
    const uint32_t marker=p.getUInt(kHistoryUpgradeKey,0);
    last_history_upgrade.store(marker>0 && marker==good?marker:0);
    enevina::cottage::OtaHistoryLast summary{};
    const bool has_summary=p.getBytesLength(kHistoryLastKey)==sizeof(summary) &&
        p.getBytes(kHistoryLastKey,&summary,sizeof(summary))==sizeof(summary) &&
        enevina::cottage::valid_ota_history_last(summary) && summary.version<=good;
    last_history_version.store(0);
    last_history_utc.store(has_summary?summary.utc:0);
    last_history_version.store(has_summary?summary.version:0);
    const uint32_t old_ack=p.getUInt(kHistoryAckKey,0);
    // A durable summary also proves the history event was already written:
    // survive a power cut between this blob and the legacy hist_done key.
    last_history_ack.store(has_summary && summary.version==good?
        summary.version:old_ack);
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
    // A failed transaction read or failed cleanup is not authorization to
    // reclaim older site settings needed by a rollback image.
    ota_storage_ready.store(last_status.load()!=3);
}
bool cottage_ota_confirmation_pending() {
    return confirmation_pending.load();
}
bool cottage_ota_runtime_ready() {
    return ota_storage_ready.load();
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
    const auto record_confirmed_version=[&](uint32_t version) {
        // The callback runs AFTER good is durable but BEFORE txv2 cleanup.
        // On a reboot good can already equal tx.version after an interrupted
        // confirmation. Retrying the same idempotent hint recovers that case.
        // The rollback-verified transaction and running image identity were
        // checked above; only a good-version match can create history intent.
        if(p.getUInt("good",0)!=version)return;
        const bool recorded=p.getUInt(kHistoryUpgradeKey,0)==version ||
            (p.putUInt(kHistoryUpgradeKey,version)==sizeof(uint32_t) &&
             p.getUInt(kHistoryUpgradeKey,0)==version);
        last_history_upgrade.store(version); // same-boot fallback on NVS failure
        if(!recorded)
            Serial.println("C24_OTA_HISTORY: event hint volatile only");
    };
    // History writes are explicitly non-gating; the OTA version/transaction
    // durable sequence is still controlled only by confirm_persistence().
    const bool success=ota::confirm_persistence(store,tx,good,record_confirmed_version);
    if(success) {
        last_good.store(std::max(good,tx.version));
        confirmation_pending.store(false);
    }
    p.end();
    return success;
}
uint32_t cottage_ota_good_version(){return last_good.load();}
uint32_t cottage_ota_recorded_upgrade_version(){
    return enevina::cottage::ota_pending_history_upgrade(
        last_good.load(),last_history_upgrade.load(),last_history_ack.load());
}
bool cottage_ota_acknowledge_history_upgrade(uint32_t version,uint32_t event_utc) {
    if(version==0 || event_utc<1500000000)return false;
    Preferences p;
    if(!p.begin("c24_ota",false))return false;
    // Only a durably confirmed signed version may enter the last-update card.
    const bool matching=p.getUInt("good",0)==version &&
        p.getUInt(kHistoryUpgradeKey,0)==version;
    if(!matching){p.end();return false;}
    enevina::cottage::OtaHistoryLast existing{};
    const bool prior=p.getBytesLength(kHistoryLastKey)==sizeof(existing) &&
        p.getBytes(kHistoryLastKey,&existing,sizeof(existing))==sizeof(existing) &&
        enevina::cottage::valid_ota_history_last(existing);
    // Retain the FIRST confirmed UTC time for the same version across reboots.
    const auto wanted=prior && existing.version==version?existing:
        enevina::cottage::make_ota_history_last(version,event_utc);
    const bool wrote=(prior && existing.version==version) ||
        p.putBytes(kHistoryLastKey,&wanted,sizeof(wanted))==sizeof(wanted);
    enevina::cottage::OtaHistoryLast readback{};
    const bool verified=wrote &&
        p.getBytesLength(kHistoryLastKey)==sizeof(readback) &&
        p.getBytes(kHistoryLastKey,&readback,sizeof(readback))==sizeof(readback) &&
        enevina::cottage::valid_ota_history_last(readback) &&
        readback.version==wanted.version && readback.utc==wanted.utc;
    if(verified && p.getUInt(kHistoryAckKey,0)!=version)
        p.putUInt(kHistoryAckKey,version); // compatibility; summary is the ack
    p.end();
    if(verified) {
        // A reader must never pair the new version with the old UTC.
        last_history_version.store(0);
        last_history_utc.store(wanted.utc);
        last_history_version.store(wanted.version);
        last_history_ack.store(version);
    }
    return verified;
}
bool cottage_ota_last_successful_update(uint32_t& utc,uint32_t& version) {
    const uint32_t v=last_history_version.load(),t=last_history_utc.load();
    if(v==0 || v!=last_history_version.load())return false;
    const auto summary=enevina::cottage::make_ota_history_last(v,t);
    if(!enevina::cottage::valid_ota_history_last(summary) ||
       v>last_good.load())return false;
    utc=t;version=v;return true;
}

uint32_t cottage_ota_last_candidate(){return last_candidate.load();}
int cottage_ota_status(){return last_status.load();}
const char* cottage_ota_problem_stage() {
    switch(static_cast<OtaProblemStage>(ota_problem_stage.load())) {
        case OtaProblemStage::ManifestLease:return "manifest_lease";
        case OtaProblemStage::Storage:return "nvs";
        case OtaProblemStage::ImageLease:return "image_lease";
        case OtaProblemStage::Transport:return "image_transfer";
        case OtaProblemStage::Flash:return "flash_programming";
        default:return "none";
    }
}
const char* cottage_ota_problem_reason() {
    switch(static_cast<OtaProblemReason>(ota_problem_reason.load())) {
        case OtaProblemReason::MutexBusy:return "mutex_busy";
        case OtaProblemReason::LowMemory:return "low_internal_heap";
        case OtaProblemReason::NvsUnavailable:return "nvs_unavailable";
        case OtaProblemReason::WifiLost:return "wifi_lost";
        case OtaProblemReason::ImageFailed:return "image_or_hash_failure";
        case OtaProblemReason::HttpOpen:return "https_image_open_failed";
        case OtaProblemReason::FlashBegin:return "ota_flash_begin_failed";
        case OtaProblemReason::WifiDuringImage:return "wifi_lost_during_image";
        case OtaProblemReason::InflightLowMemory:return "inflight_heap_below_24k";
        case OtaProblemReason::HttpReadError:return "https_image_read_error";
        case OtaProblemReason::HttpReadTimeout:return "https_image_read_timeout";
        case OtaProblemReason::ShaFailed:return "sha256_computation_failed";
        case OtaProblemReason::FlashWriteFailed:return "ota_flash_write_failed";
        case OtaProblemReason::WrongLength:return "image_length_mismatch";
        case OtaProblemReason::DigestMismatch:return "image_sha256_mismatch";
        case OtaProblemReason::FlashEndFailed:return "ota_flash_finalize_failed";
        case OtaProblemReason::ImageShaFailed:return "flashed_image_hash_failed";
        case OtaProblemReason::NvsWriteFailed:return "nvs_transaction_write_failed";
        case OtaProblemReason::BootSelectFailed:return "ota_boot_selection_failed";
        case OtaProblemReason::PsramUnavailable:return "ota_psram_staging_unavailable";
        case OtaProblemReason::TransferDeadline:return "ota_no_progress";
        case OtaProblemReason::RangeMismatch:return "ota_range_identity_mismatch";
        case OtaProblemReason::HistoryFlushFailed:return "ota_history_flush_failed";
        case OtaProblemReason::LearningFlushFailed:return "ota_learning_flush_failed";
        case OtaProblemReason::RetryRequiresRequest:return "retry_requires_request";
        default:return "none";
    }
}
uint32_t cottage_ota_problem_free_internal(){return ota_problem_free.load();}
uint32_t cottage_ota_problem_largest_internal(){return ota_problem_largest.load();}
uint32_t cottage_ota_image_lease_attempts(){return ota_image_attempts.load();}
uint32_t cottage_ota_inflight_memory_waits(){return ota_inflight_memory_waits.load();}
bool cottage_ota_network_maintenance(){return pause_readers.load();}
uint32_t cottage_ota_downloaded_bytes(){return ota_written_bytes.load();}
uint32_t cottage_ota_expected_bytes(){return ota_expected_bytes.load();}
uint32_t cottage_ota_resume_count(){return ota_resume_count.load();}
uint32_t cottage_ota_durable_bytes(){return ota_durable_bytes.load();}
bool cottage_ota_reconnecting(){return ota_reconnecting.load();}
uint32_t cottage_ota_programmed_bytes(){return ota_programmed_bytes.load();}
uint32_t cottage_ota_chunk_count(){return ota_completed_chunks.load();}
uint32_t cottage_ota_idle_yields(){return ota_flash_yields.load();}
bool cottage_ota_request_immediate() {
    const auto task=ota_worker_task.load();
    if(!cottage_ota_configured() || cottage_ota_confirmation_pending() ||
       !task || pause_readers.load() || last_status.load()==5 || last_status.load()==6)return false;
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
    // The first check services any dashboard request queued during boot.
    // Consume only its wakeup now: otherwise the stale notification would
    // wrongly skip the ten-minute cooldown if that first check fails.
    // Do not drain at the top of every loop: requests arriving during an
    // in-progress check must still wake the subsequent retry wait.
    ulTaskNotifyTake(pdTRUE,0);
    while(true) {
        const bool explicit_request=ota_manual_pending.exchange(false);
        ota_reset_problem();
        const time_t now=time(nullptr);
        if(now>1700000000)ota_last_checked_utc.store(static_cast<uint32_t>(now));
        // Keep normal CN105/Shelly readers running during routine manifest
        // checks. Briefly pause ONLY after the signed version is eligible.
        // Every exit path from an attempted update restores reader activity.
        cottage_ota_initialize();
        if(WiFi.status()!=WL_CONNECTED || time(nullptr)<1700000000) {
            if(explicit_request)ota_manual_pending.store(true);
            last_status.store(2);
        } else {
            enevina_firmware::NetworkBudgetDiagnostic manifest_diag{};
            if(!enevina_firmware::network_budget_acquire(5000,
                    enevina::cottage::ota_manifest_min_internal_bytes,
                    enevina::cottage::ota_min_contiguous_internal_bytes,&manifest_diag)) {
                ota_record_problem(OtaProblemStage::ManifestLease,
                                   ota_budget_reason(manifest_diag.failure),manifest_diag);
                last_status.store(3);
                if(explicit_request)ota_manual_pending.store(true);
                // Preserve the single user request across temporary resource contention.
                // The original control flow expects the lock only when
                // acquired. Continue at the same bounded 60-second cadence.
                ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(60000));
                continue;
            }
            Release r{};
            const auto manifest=fetch_manifest(r);
            if(manifest!=ManifestResult::Valid) {
                if(manifest==ManifestResult::Unavailable) {
                    if(explicit_request)ota_manual_pending.store(true);
                    last_status.store(2);
                } else if(manifest==ManifestResult::NotOffered) {
                    last_candidate.store(0);
                    ota_reconnecting.store(false);
                    last_status.store(0);
                } else last_status.store(4);
            } else {
                last_candidate.store(r.version);
                const auto identity=ota::spool_identity(r.version,r.size,r.digest);
                Preferences p;
                uint32_t good=0,bad=0,pending=0,attempted=0;
                bool store_ready=p.begin("c24_ota",true);
                if(store_ready) {
                    good=p.getUInt("good",0);
                    bad=p.getUInt("bad",0);
                    pending=p.getUInt("pending",0);
                    // txv2 is authoritative for all new OTA releases.
                    if(p.isKey("txv2"))pending=1;
                    p.end();
                    store_ready=read_attempted_version(attempted);
                }
                const bool retry=store_ready && retry_identity_matches(attempted,identity);
                if(!store_ready) {
                    ota_record_problem(OtaProblemStage::Storage,
                                       OtaProblemReason::NvsUnavailable);
                    last_status.store(3);
                } else if(!ota::signed_version_eligible(r.version,good,bad,pending!=0)) {
                    last_status.store(0);
                } else if(!ota::image_attempt_allowed(r.version,attempted,explicit_request||retry)) {
                    ota_record_problem(OtaProblemStage::Transport,
                                       OtaProblemReason::RetryRequiresRequest);
                    last_status.store(4);
                } else {
                    // The manifest TLS session has already been cleaned up.
                    // Release its shared network lock before allowing readers
                    // to drain: SSE may hold a separate, persistent socket.
                    enevina_firmware::network_budget_release();
                    bool applied=false,transient=false;
                    if(!remember_image_attempt(identity,explicit_request||retry)) {
                        ota_record_problem(OtaProblemStage::Storage,
                                           OtaProblemReason::NvsWriteFailed);
                        last_status.store(3);
                    } else {
                        last_status.store(5);
                        applied=update_app(r);
                        if(!applied && ota_problem_stage.load()==0)
                            ota_record_problem(OtaProblemStage::Transport,OtaProblemReason::ImageFailed);
                        transient=!applied&&retryable_image_failure();
                        if(applied || !transient) {
                            const bool cleared=clear_retry_identity();
                            if(!cleared)Serial.println("C24_OTA: retry marker cleanup failed");
                            const auto problem=static_cast<OtaProblemReason>(ota_problem_reason.load());
                            // Keep a fully verified cache across local storage
                            // failures. A rejected payload may never be reused.
                            if(!applied && (problem==OtaProblemReason::RangeMismatch ||
                               problem==OtaProblemReason::WrongLength || problem==OtaProblemReason::DigestMismatch))
                                enevina_firmware::history_ota_spool_discard();
                        }
                        ota_reconnecting.store(transient);
                        // Consume only the attempt's old notification while
                        // status=5 still rejects a new dashboard request.
                        ota_manual_pending.exchange(false);
                        ulTaskNotifyTake(pdTRUE,0);
                        last_status.store(applied?6:(transient?2:4));
                    }
                    if(!applied)enevina_firmware::history_resume_after_ota();
                    pause_readers.store(false);
                    if(applied) {
                        Serial.println("C24_SIGNED_OTA_VERIFIED: rebooting into standby partition");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    }
                    // Only a transient failure of this exact signed identity
                    // may retry autonomously. A digest/signature/activation
                    // failure remains blocked; the old app always runs.
                    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(transient?60000u:10*60*1000u));
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