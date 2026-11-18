#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <cJSON.h>
#include <array>
#include <algorithm>
#include <memory>
#include <new>
#include <atomic>
#include <cmath>
#include <ctime>
#include <cstring>
#include <string>
#include "cottage_config.hpp"
#include "cottage_premium_dashboard.hpp"
#include "cottage_air_service.hpp"
#include "cottage_network_budget.hpp"
#include "cottage_secure_ota.hpp"
#include "cottage/physics.hpp"
#include "cottage/mpc.hpp"
#include "cottage/telemetry.hpp"
#include "cottage/supervisor.hpp"
#include "cottage/forecast_bridge.hpp"
#include "cottage/ruuvi.hpp"
#include "cottage/url_guard.hpp"
#include "cottage/mode_timing.hpp"
#include "cottage/price_bootstrap.hpp"
#include "cottage/mitsubishi_climate_command.hpp"
#include "cottage/mitsubishi_control_policy.hpp"
#include "cottage/finnish_tariff.hpp"
#include "cottage/sse_transport.hpp"
#include "cottage/cn105_source_proof.hpp"
#include "../../main/market.hpp"
#include "../../main/weather.hpp"

// Arduino ESP32 initArduino() calls verifyOta() and marks a PENDING_VERIFY
// firmware valid by default BEFORE setup(). Override its documented weak
// hook so only Enevina's own post-boot health gate commits a new image.
extern "C" bool verifyRollbackLater() { return true; }

using namespace enevina::cottage;
static WebServer web(80);
static SemaphoreHandle_t lock_data=nullptr;
static std::atomic<bool> wifi_ok{false};
static std::atomic<uint32_t> web_loop_heartbeat_ms{0};
static CN105Telemetry pump{};
// Legacy ESPHome /events exposes real device state as SSE, but the receipt
// does not prove a new UART poll. Keep the last observed values separately
// from the source-verified sample used by the optimizer/safety gate.
static CN105Telemetry sse_observed_pump{};
static ObservedModeDuration observed_mode_duration{};
// Protected by lock_data. Reject MPC results computed before an SSE mode change.
static uint32_t observed_mode_generation=0;
static ShellyTelemetry consumption{};
static TemperatureSample temperature{};
static int64_t climate_utc=0,compressor_utc=0;
static MpcResult last_plan{};
static BridgeStatus bridge_status=BridgeStatus::Invalid;
static Decision safety{};
static int64_t last_optimized_utc=0;
static uint32_t optimizer_ms=0;
static uint32_t max_optimizer_ms=0;
static size_t plan_count=0;
static uint32_t free_heap=0,free_psram=0,stack_watermark=0;
static uint32_t boot_validation_started_ms=0;
// Captured once at boot for diagnosing uptime drops (no watchdog/reset).
static int boot_reset_reason_code=-1;
static bool boot_ui_started=false;
static bool ota_boot_confirmed=false;
static std::atomic<uint32_t> failed_reads{0};
static std::atomic<uint32_t> sse_connections{0},sse_disconnects{0};
static std::atomic<uint32_t> sse_climate_events{0},sse_compressor_events{0};
static std::atomic<uint32_t> sse_link_events{0};
static std::atomic<bool> sse_stream_active{false};
static std::atomic<int> sse_last_http_status{0};
static std::atomic<uint32_t> shelly_reads_ok{0},shelly_reads_bad{0};
static std::atomic<uint32_t> shelly_rtt_ms{0};
static int64_t cn105_link_event_utc=0;
static double cn105_link_uptime_s=0;
// Used only by the existing CN105 SSE worker; no extra task/watchdog.
static CN105SourceProofVerifier cn105_source_verifier{};
static bool cn105_proof_envelope_active=false;
static std::atomic<int> cn105_source_proof_status{0};
static std::atomic<uint32_t> cn105_source_proof_good{0};
static TaskHandle_t task_sse=nullptr,task_shelly=nullptr;
static TaskHandle_t task_ruuvi=nullptr,task_optimizer=nullptr;
static TaskHandle_t task_ota=nullptr;
static bool shelly_relay_on=false;
// Production control is explicitly ARMED via local setting, never by OTA.
static std::atomic<bool> control_armed{false};
static std::atomic<int> control_gate{static_cast<int>(ControlBlock::Disabled)};
static std::atomic<int> control_last_step{static_cast<int>(ControlStep::None)};
static std::atomic<uint32_t> control_commands_sent{0};
static std::atomic<uint32_t> control_commands_confirmed{0};
static std::atomic<uint32_t> control_commands_failed{0};
static std::atomic<int64_t> control_last_command_utc{0};
static std::atomic<int64_t> control_last_ack_utc{0};
static std::atomic<int64_t> control_last_bridge_read_utc{0};
static char reason_text[80]="Käynnistys";
struct Row {
    int64_t utc=0;
    double indoor=0;
    double electric=0;
    double price=0;
    bool heat16=false;
};
static std::array<Row,max_horizon_slots> plan_rows{};
static Parameters model{};

// All cJSON nodes and serialized HTTP JSON are allocated in PSRAM.
// Never fall back to scarce internal RAM on allocation failure.
static void* json_alloc_psram(size_t n) {
    return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
static void json_free_psram(void* p) {heap_caps_free(p);}
// A/B OTA boot verification: no new image is confirmed immediately merely
// because the bootloader reached setup(). With an ESP-IDF rollback-capable
// bootloader, a boot failure or reset before confirmation falls back safely.
static void verify_new_ota_boot() {
    const esp_partition_t* running=esp_ota_get_running_partition();
    esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
    if(!running || esp_ota_get_state_partition(running,&state)!=ESP_OK ||
       (state!=ESP_OTA_IMG_PENDING_VERIFY && state!=ESP_OTA_IMG_VALID))return;
    const bool boot_pending=state==ESP_OTA_IMG_PENDING_VERIFY;
    const uint32_t elapsed=millis()-boot_validation_started_ms;
    if(!cottage_ota_confirmation_pending()) {
        // Legacy NVS migration or transaction read may have failed.
        // Do not accept the candidate without a durable release identity.
        if(boot_pending && elapsed>120000)
            esp_ota_mark_app_invalid_rollback_and_reboot();
        return;
    }
#ifdef ENEVINA_OTA_TEST_RESET_BEFORE_CONFIRM
    // Explicitly gated physical regression test: simulate sudden app reset
    // while the new OTA image is pending. Only enable in a special signed
    // fault-test release, never in the field/production firmware.
    if(boot_pending && elapsed>=15000) {
        Serial.println("C24_OTA_TEST: restarting before confirmation");
        Serial.flush();
        esp_restart();
    }
#endif
    if(elapsed<60000) return; // allow BLE, WebServer, RTOS to settle
    // Never confirm a candidate that cannot receive the next signed OTA.
    const bool healthy=cottage_ota_configured() &&
        boot_ui_started && wifi_ok.load() &&
        c23_ble_initialized() &&
        ESP.getPsramSize()>=7*1024*1024 &&
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>=64*1024 &&
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>=16*1024 &&
        task_sse!=nullptr && task_shelly!=nullptr &&
        task_optimizer!=nullptr && task_ota!=nullptr;
    if(healthy) {
        // Power loss can occur after bootloader VALID and before NVS good.
        // Retry the same durable NVS transaction on both PENDING and VALID
        // boots. Never claim success after a failed NVS put/remove.
        if(boot_pending && esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK) {
            Serial.println("C24_OTA_ERROR: boot validation could not commit");
            return;
        }
        if(cottage_ota_confirm_boot())ota_boot_confirmed=true;
        else {
            static uint32_t last_nvs_error_ms=0;
            if(millis()-last_nvs_error_ms>=30000u){
                last_nvs_error_ms=millis();
                Serial.println("C24_OTA_ERROR: NVS confirmation failed; retrying");
            }
        }
    } else if(boot_pending && elapsed>120000) {
        Serial.println("C24_OTA_REJECT: boot self-test failed; requesting rollback");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}
static bool clock_ok() {return time(nullptr)>1700000000;}
static bool number(const cJSON* item,double& output,double min,double max) {
    if(!item) return false;
    if(cJSON_IsNumber(item)) output=item->valuedouble;
    else if(cJSON_IsString(item)) {
        char* end=nullptr;
        output=strtod(item->valuestring,&end);
        if(end==item->valuestring || *end) return false;
    } else return false;
    return std::isfinite(output) && output>=min && output<=max;
}
// Optional read-only source-verification event (CN105 pilot adapter only).
// The upstream bridge must construct this one JSON value from *successful*
// 0x02, 0x03 and 0x06 UART decodes in the same poll, never from cached SSE.
static bool cn105_proof_unsigned(const cJSON* doc,const char* name,
                                 uint64_t max,uint64_t& out) {
    const cJSON* item=cJSON_GetObjectItemCaseSensitive(doc,name);
    if(!cJSON_IsNumber(item))return false;
    const double value=item->valuedouble;
    if(!std::isfinite(value) || value<1 || value>static_cast<double>(max) ||
       std::floor(value)!=value)return false;
    out=static_cast<uint64_t>(value);
    return true;
}
static bool cn105_proof_timestamp(const cJSON* doc,const char* name,int64_t& out) {
    uint64_t value=0;
    if(!cn105_proof_unsigned(doc,name,4102444800ULL,value) ||
       value<1700000000ULL)return false;
    out=static_cast<int64_t>(value);
    return true;
}
static bool parse_cn105_proof_event(const cJSON* root,int64_t now) {
    const cJSON* source=cJSON_GetObjectItemCaseSensitive(root,"value");
    if(!cJSON_IsString(source) || !source->valuestring ||
       strlen(source->valuestring)>512)return false;
    // Parse the ENTIRE proof envelope: cJSON_Parse() silently accepts
    // concatenated JSON or trailing garbage after a valid first object.
    cJSON* doc=cJSON_ParseWithOpts(source->valuestring,nullptr,true);
    if(!doc)return false;
    uint64_t schema=0,boot=0,seq=0;
    CN105ProvenPoll p{};
    p.received_utc=now;
    const cJSON* uart=cJSON_GetObjectItemCaseSensitive(doc,"uart_connected");
    const cJSON* complete=cJSON_GetObjectItemCaseSensitive(doc,"complete_poll");
    const cJSON* mode=cJSON_GetObjectItemCaseSensitive(doc,"mode");
    const bool ok=cn105_proof_unsigned(doc,"schema",255,schema) &&
        cn105_proof_unsigned(doc,"boot",UINT32_MAX,boot) &&
        cn105_proof_unsigned(doc,"seq",UINT32_MAX,seq) &&
        cn105_proof_timestamp(doc,"oldest_utc",p.oldest_uart_read_utc) &&
        cn105_proof_timestamp(doc,"latest_utc",p.latest_uart_read_utc) &&
        cJSON_IsBool(uart) && cJSON_IsBool(complete) &&
        cJSON_IsString(mode) &&
        number(cJSON_GetObjectItemCaseSensitive(doc,"indoor_c"),p.indoor_c,-10,50) &&
        number(cJSON_GetObjectItemCaseSensitive(doc,"setpoint_c"),p.setpoint_c,0,35) &&
        number(cJSON_GetObjectItemCaseSensitive(doc,"compressor_hz"),p.compressor_hz,0,200);
    if(ok) {
        p.schema=static_cast<uint32_t>(schema);
        p.boot_epoch=boot;p.completed_poll_sequence=seq;
        p.uart_connected=cJSON_IsTrue(uart);
        p.complete_decoded_poll=cJSON_IsTrue(complete);
        if(strcmp(mode->valuestring,"HEAT")==0) p.mode=ObservedPumpMode::Heat;
        else if(strcmp(mode->valuestring,"OFF")==0) p.mode=ObservedPumpMode::Off;
        else if(strcmp(mode->valuestring,"OTHER")==0) p.mode=ObservedPumpMode::Other;
    }
    cJSON_Delete(doc);
    if(!ok || p.mode==ObservedPumpMode::Unknown)return false;
    CN105Telemetry verified{};
    const auto proof=cn105_source_verifier.accept(p,verified);
    cn105_source_proof_status.store(static_cast<int>(proof));
    if(proof==enevina::PumpReadProofStatus::NewRead) {
        xSemaphoreTake(lock_data,portMAX_DELAY);
        const bool continuous=valid_cn105(pump,now);
        // A newly verified pump mode or a break in confirmed dwell invalidates
        // the optimizer's snapshot before the fresh sample is published.
        if(verified.mode!=pump.mode || !continuous) {
            ++observed_mode_generation;
            plan_count=0;
            last_plan.status=MpcStatus::InvalidInput;
        }
        observed_mode_duration.observe(verified.mode,
            static_cast<uint64_t>(esp_timer_get_time()/1000),continuous);
        pump=verified;
        xSemaphoreGive(lock_data);
        cn105_source_proof_good.fetch_add(1);
    } else if(proof!=enevina::PumpReadProofStatus::Replay) {
        xSemaphoreTake(lock_data,portMAX_DELAY);
        pump.received=false;
        pump.evidence.source_time_verified=false;
        observed_mode_duration.invalidate();
        ++observed_mode_generation;
        plan_count=0;
        last_plan.status=MpcStatus::InvalidInput;
        xSemaphoreGive(lock_data);
    }
    return true;
}
static void set_reason(const char* msg) {
    xSemaphoreTake(lock_data,portMAX_DELAY);
    strlcpy(reason_text,msg,sizeof(reason_text));
    xSemaphoreGive(lock_data);
}
static bool get_http(const char* url,String& result,unsigned timeout_ms=4000) {
    if(!url||strncmp(url,"http://",7)!=0||WiFi.status()!=WL_CONNECTED) return false;
    WiFiClient client;
    HTTPClient request;
    request.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    request.setTimeout(timeout_ms);
    request.setConnectTimeout(1500);
    if(!request.begin(client,url)) return false;
    int status=request.GET();
    // Shelly Gen2 supplies Content-Length. Never request an unbounded
    // response body, even if the local device is misconfigured.
    const int body_size=request.getSize();
    const bool ok=status==200 && body_size>0 && body_size<=4096 &&
        request.header("Transfer-Encoding").length()==0;
    if(ok) result=request.getString();
    request.end();
    return ok && result.length()==static_cast<unsigned>(body_size);
}
static void parse_sse_state(const char* raw) {
    cJSON* root=cJSON_ParseWithOpts(raw,nullptr,true);
    if(!root) return;
    const cJSON* key=cJSON_GetObjectItemCaseSensitive(root,"id");
    const int64_t now=clock_ok()?time(nullptr):0;
    if(!cJSON_IsString(key)||!now) {cJSON_Delete(root);return;}
    if(strcmp(key->valuestring,"text_sensor/Enevina CN105 Verified Snapshot")==0) {
        if(parse_cn105_proof_event(root,now))cn105_proof_envelope_active=true;
        else {
            cn105_source_proof_status.store(
                static_cast<int>(enevina::PumpReadProofStatus::Unverified));
            xSemaphoreTake(lock_data,portMAX_DELAY);
            pump.received=false;
            pump.evidence.source_time_verified=false;
            observed_mode_duration.invalidate();
            ++observed_mode_generation;
            plan_count=0;
            last_plan.status=MpcStatus::InvalidInput;
            xSemaphoreGive(lock_data);
        }
        cJSON_Delete(root);
        return;
    }
    // Once the source-signed poll envelope is observed, legacy individual
    // climate/compressor SSE snapshots cannot overwrite its proven sample.
    if(cn105_proof_envelope_active &&
       (strcmp(key->valuestring,"climate/Mitsubishi Electric MSZ-FD25VA")==0 ||
        strcmp(key->valuestring,"sensor/Compressor Frequency")==0)) {
        cJSON_Delete(root);
        return;
    }
    CN105Telemetry copy;
    xSemaphoreTake(lock_data,portMAX_DELAY);copy=sse_observed_pump;xSemaphoreGive(lock_data);
    bool changed=false,climate_event=false;
    int64_t climate_stamp=climate_utc,compressor_stamp=compressor_utc;
    if(strcmp(key->valuestring,"climate/Mitsubishi Electric MSZ-FD25VA")==0) {
        const cJSON* mode=cJSON_GetObjectItemCaseSensitive(root,"mode");
        double indoor=0,target=0;
        if(cJSON_IsString(mode) &&
           number(cJSON_GetObjectItemCaseSensitive(root,"current_temperature"),indoor,-10,50) &&
           number(cJSON_GetObjectItemCaseSensitive(root,"target_temperature"),target,0,35)) {
            if(strcmp(mode->valuestring,"OFF")==0) copy.mode=ObservedPumpMode::Off;
            else if(strcmp(mode->valuestring,"HEAT")==0) copy.mode=ObservedPumpMode::Heat;
            else copy.mode=ObservedPumpMode::Other;
            copy.indoor_c=indoor;copy.setpoint_c=target;
            climate_stamp=now;changed=true;climate_event=true;
            sse_climate_events.fetch_add(1);
        }
    } else if(strcmp(key->valuestring,"sensor/Compressor Frequency")==0) {
        double hz=0;
        if(number(cJSON_GetObjectItemCaseSensitive(root,"value"),hz,0,200)) {
            copy.compressor_hz=hz;compressor_stamp=now;changed=true;
            sse_compressor_events.fetch_add(1);
        }
    } else if(strcmp(key->valuestring,"sensor/CN105 Connection Uptime")==0) {
        double uptime_s=0;
        if(number(cJSON_GetObjectItemCaseSensitive(root,"value"),uptime_s,0,1e10)) {
            // Link heartbeat is NOT proof of a fresh pump-temperature read.
            xSemaphoreTake(lock_data,portMAX_DELAY);
            cn105_link_event_utc=now;
            cn105_link_uptime_s=uptime_s;
            xSemaphoreGive(lock_data);
            sse_link_events.fetch_add(1);
        }
    }
    cJSON_Delete(root);
    if(changed) {
        xSemaphoreTake(lock_data,portMAX_DELAY);
        if(climate_event) {
            const bool continuous=climate_utc>0 && now>=climate_utc &&
                now-climate_utc<=180;
            // The MPC snapshot assumes both its original pump mode AND
            // confirmed uninterrupted dwell age. A gap >180s or a clock
            // discontinuity resets dwell even when HEAT/OFF is unchanged.
            // Change generation under lock_data so a concurrently running
            // optimizer cannot publish a schedule built on stale dwell.
            if(copy.mode!=pump.mode || !continuous) {
                ++observed_mode_generation;
                plan_count=0;
                last_plan.status=MpcStatus::InvalidInput;
            }
            observed_mode_duration.observe(copy.mode,
                static_cast<uint64_t>(esp_timer_get_time()/1000),continuous);
        }
        climate_utc=climate_stamp;compressor_utc=compressor_stamp;
        // /events may replay cached climate and compressor snapshots after
        // a reconnect. climate_utc/compressor_utc above record SSE RECEIPT
        // time only, not a proven pump UART read or source sequence. Until
        // the CN105 bridge supplies verifiable pump-origin freshness, fail
        // closed: never make receipt time look like fresh pump telemetry.
        copy.captured_utc=0;
        copy.received=false;
        // The legacy CN105 bridge exposes only SSE receipt time. Explicit
        // unverified provenance must never turn cached SSE into pump freshness.
        copy.evidence={now,0,false};
        sse_observed_pump=copy;
        // The existing strict source-verification gate remains closed for
        // a legacy SSE snapshot. No synthetic UART timestamp is created.
        pump=copy;
        xSemaphoreGive(lock_data);
    }
}
static void sse_worker(void*) {
    for(;;) {
        if(!wifi_ok.load() || !clock_ok() || !valid_cn105_events_url(COTTAGE_CN105_URL)) {
            vTaskDelay(pdMS_TO_TICKS(1000));continue;
        }
        WiFiClient client;
        HTTPClient request;
        request.setConnectTimeout(2000);
        request.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        request.setTimeout(2500);
        request.addHeader("Accept","text/event-stream");
        request.addHeader("Accept-Encoding","identity");
        static const char* headers[]={"Content-Type","Transfer-Encoding"};
        request.collectHeaders(headers,2);
        const bool began=request.begin(client,COTTAGE_CN105_URL);
        const int http_status=began?request.GET():0;
        sse_last_http_status.store(http_status);
        if(http_status==200) {
            const String content_type=request.header("Content-Type");
            const String transfer=request.header("Transfer-Encoding");
            const bool chunked=transfer.equalsIgnoreCase("chunked");
            const bool plain=transfer.length()==0 ||
                transfer.equalsIgnoreCase("identity");
            if(content_type.indexOf("text/event-stream")<0 ||
               (!chunked && !plain)) {
                failed_reads++;
            } else {
                sse_stream_active.store(true);
                sse_connections.fetch_add(1);
                cn105_source_verifier.reconnect();
                cn105_proof_envelope_active=false;
                cn105_source_proof_status.store(
                    static_cast<int>(enevina::PumpReadProofStatus::Unverified));
                xSemaphoreTake(lock_data,portMAX_DELAY);
                pump.received=false;
                pump.evidence.source_time_verified=false;
                observed_mode_duration.invalidate();
                ++observed_mode_generation;
                plan_count=0;
                last_plan.status=MpcStatus::InvalidInput;
                xSemaphoreGive(lock_data);
                WiFiClient* stream=request.getStreamPtr();
                HttpChunkDecoder decoder;
                SseStateParser parser([](const char* json,void*) {
                    parse_sse_state(json);
                },nullptr);
                uint32_t last_activity_ms=millis();
                while(wifi_ok.load() && client.connected() &&
                      static_cast<uint32_t>(millis()-last_activity_ms)<180000u) {
                    if(!stream->available()) {
                        vTaskDelay(pdMS_TO_TICKS(20));continue;
                    }
                    const int input=stream->read();
                    if(input<0)continue;
                    last_activity_ms=millis();
                    uint8_t body=0;
                    bool produced=!chunked;
                    if(chunked && !decoder.feed(static_cast<uint8_t>(input),
                                                body,produced)) {
                        failed_reads++;
                        break;
                    }
                    if(produced) parser.feed(chunked?body:static_cast<uint8_t>(input));
                    if(chunked && decoder.finished())break;
                }
                sse_stream_active.store(false);
                sse_disconnects.fetch_add(1);
            }
        } else { failed_reads++; }
        request.end();
        // The mode could have changed while the SSE stream was down.
        // Resume conservatively with zero confirmed dwell age.
        xSemaphoreTake(lock_data,portMAX_DELAY);
        observed_mode_duration.invalidate();
        // A disconnected SSE stream is neither a fresh pump read nor
        // evidence of unchanged dwell. Revoke both freshness and all
        // cached/in-flight optimizer plans under the same data lock.
        pump.received=false;
        pump.evidence.source_time_verified=false;
        ++observed_mode_generation;
        plan_count=0;
        last_plan.status=MpcStatus::InvalidInput;
        xSemaphoreGive(lock_data);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
static void shelly_worker(void*) {
    char url[192];
    snprintf(url,sizeof(url),"%s/rpc/Switch.GetStatus?id=0",COTTAGE_SHELLY_URL);
    for(;;) {
        if(wifi_ok.load() && clock_ok() &&
           valid_shelly_base_url(COTTAGE_SHELLY_URL)) {
            String body;
            const uint32_t req_begin_ms=millis();
            if(get_http(url,body)) {
                cJSON* root=cJSON_Parse(body.c_str());
                double watts=0,wh=0;
                cJSON* energy=cJSON_GetObjectItemCaseSensitive(root,"aenergy");
                cJSON* output=cJSON_GetObjectItemCaseSensitive(root,"output");
                if(root && cJSON_IsBool(output) &&
                   number(cJSON_GetObjectItemCaseSensitive(root,"apower"),watts,0,20000) &&
                   number(cJSON_GetObjectItemCaseSensitive(energy,"total"),wh,0,1e12)) {
                    ShellyTelemetry next{true,time(nullptr),watts,wh};
                    xSemaphoreTake(lock_data,portMAX_DELAY);consumption=next;
                    shelly_relay_on=cJSON_IsTrue(output);
                    xSemaphoreGive(lock_data);
                    shelly_rtt_ms.store(static_cast<uint32_t>(millis()-req_begin_ms));
                    shelly_reads_ok.fetch_add(1);
                } else {failed_reads++;shelly_reads_bad.fetch_add(1);}
                cJSON_Delete(root);
            } else {failed_reads++;shelly_reads_bad.fetch_add(1);}
        }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
static enevina::Tariff tariff(bool night=false) {
    enevina::Tariff t;
    t.margin_eur_per_kwh=COTTAGE_SPOT_MARGIN;
    t.transfer_eur_per_kwh=night?COTTAGE_TRANSFER_NIGHT:COTTAGE_TRANSFER_DAY;
    t.electricity_tax_eur_per_kwh=COTTAGE_ELECTRICITY_TAX;
    t.vat_rate=COTTAGE_VAT_RATE;
    return t;
}
// Model-free price-aware heating proposal, used only until calibrated MPC
// exists. A 2-hour window must contain 8 contiguous verified 15-minute
// TOTAL marginal-cost slots. This function never emits an actuator command.
static BootstrapDecision bootstrap_preview(int64_t now,
                                            const TemperatureSample& t,
                                            const CN105Telemetry& confirmed) {
    BootstrapInput input{};
    input.indoor_fresh=valid_temperature(t,now);
    input.indoor_c=t.indoor_c;
    const CottageModeConfig mode=c23_mode_config();
    // Distinct user-configured room targets: occupied living and
    // maintenance are never overwritten by switching to the other mode.
    // Cold/Drying retain their existing non-actuating preview baseline.
    input.desired_room_c=mode.mode==CottageMode::Occupied?
        mode.occupied_target_c:
        mode.mode==CottageMode::Hold?mode.hold_target_c:16.0;
    input.currently_heating=confirmed.mode==ObservedPumpMode::Heat &&
                            valid_cn105(confirmed,now);
    // A replayed SSE snapshot must never certify HEAT/OFF dwell.
    input.confirmed_minutes_in_mode=0;
    const auto market=enevina_firmware::market_status();
    constexpr size_t N=8;
    std::array<enevina_firmware::PricePoint,N> points{};
    std::array<double,N> prices{};
    const int64_t start=(now/900)*900;
    const bool eligible=now>=1700000000 && market.clock_trusted &&
        market.current_price_valid && market.last_fetch_utc<=now &&
        market.last_fetch_utc>=now-36*3600 &&
        market.valid_until_utc>=start+static_cast<int64_t>(N)*900;
    bool valid_prices=eligible && enevina_firmware::future_prices(
        points.data(),points.size())==N;
    for(size_t i=0;valid_prices && i<N;++i) {
        if(points[i].start_utc!=start+static_cast<int64_t>(i)*900) {
            valid_prices=false;break;
        }
        bool day=false;
        if(!finnish_day_transfer(points[i].start_utc,day)) {
            valid_prices=false;break;
        }
        auto priced=tariff(!day);
        priced.spot_eur_per_kwh=points[i].spot_eur_per_kwh;
        if(!enevina::calculate_total_price(priced,prices[i])) {
            valid_prices=false;break;
        }
    }
    input.next_total_prices=prices.data();
    input.price_count=N;
    input.prices_contiguous_and_fresh=valid_prices;
    return bootstrap_price_decision(input);
}
// CN105 is a separate local ESPHome gateway. These HTTP calls are the only
// physical write path and are permitted solely by an explicitly armed
// pilot plus fresh independent sensor, prices and gateway telemetry.
static String cn105_local_url(const char* suffix) {
    if(!suffix || !valid_cn105_events_url(COTTAGE_CN105_URL))return {};
    String root(COTTAGE_CN105_URL);
    root.remove(root.length()-strlen("/events"));
    root+=suffix;
    return root;
}
static bool cn105_bridge_get(bool& heating,double& target) {
    const String url=cn105_local_url(
        "/climate/Mitsubishi%20Electric%20MSZ-FD25VA?detail=all");
    if(url.isEmpty()||!wifi_ok.load())return false;
    enevina_firmware::NetworkBudgetLease lease(1000,72*1024,16*1024);
    if(!lease.held())return false;
    WiFiClient client;
    HTTPClient req;
    req.setConnectTimeout(1500);req.setTimeout(1800);
    req.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!req.begin(client,url))return false;
    req.addHeader("Cache-Control","no-cache");
    const int code=req.GET();
    const int body_size=req.getSize();
    bool good=false;
    if(code==200&&body_size>0&&body_size<1800) {
        const String body=req.getString();
        if(body.length()==static_cast<unsigned>(body_size)) {
            cJSON* root=cJSON_ParseWithOpts(body.c_str(),nullptr,true);
            if(root) {
                const cJSON* id=cJSON_GetObjectItemCaseSensitive(root,"id");
                const cJSON* mode=cJSON_GetObjectItemCaseSensitive(root,"mode");
                const cJSON* set=cJSON_GetObjectItemCaseSensitive(root,"target_temperature");
                double setpoint=0;
                if(cJSON_IsString(id) && cJSON_IsString(mode) &&
                    strcmp(id->valuestring,
                           "climate/Mitsubishi Electric MSZ-FD25VA")==0 &&
                    number(set,setpoint,16,26) &&
                    mitsubishi_valid_setpoint(setpoint) &&
                    (strcmp(mode->valuestring,"HEAT")==0 ||
                     strcmp(mode->valuestring,"OFF")==0)) {
                    heating=strcmp(mode->valuestring,"HEAT")==0;
                    target=setpoint;
                    good=true;
                }
                cJSON_Delete(root);
            }
        }
    }
    req.end();
    if(good)control_last_bridge_read_utc.store(time(nullptr));
    return good;
}
static bool cn105_bridge_post(const MitsubishiHttpRequest& command,
                              uint32_t config_generation,ControlStep step,
                              double required_target) {
    if(!command.valid || !control_armed.load() || !wifi_ok.load())return false;
    const String url=cn105_local_url(command.path.data());
    if(url.isEmpty())return false;
    enevina_firmware::NetworkBudgetLease lease(1000,72*1024,16*1024);
    if(!lease.held())return false;
    // Network-budget wait is a cancellation point. Never dispatch a command
    // prepared before a completed DISARM, mode or target configuration change.
    if(!control_armed.load() || c23_config_generation()!=config_generation ||
       (c23_mode_config().mode!=CottageMode::Hold &&
        c23_mode_config().mode!=CottageMode::Occupied) || !wifi_ok.load() ||
       !clock_ok())return false;
    const int64_t now=time(nullptr);
    const auto current_temp=c23_temperature_sample();
    CN105Telemetry observed{};
    int64_t current_link_utc=0;
    xSemaphoreTake(lock_data,portMAX_DELAY);
    observed=sse_observed_pump;current_link_utc=cn105_link_event_utc;
    xSemaphoreGive(lock_data);
    // ESPHome GET returns the bridge's reported state. The optional
    // Verified Snapshot UART proof is never a prerequisite for this pilot.
    if(!valid_temperature(current_temp,now) ||
       !sse_stream_active.load() || current_link_utc<=0 ||
       now<current_link_utc || now-current_link_utc>180)return false;
    const auto latest=bootstrap_preview(now,current_temp,observed);
    if((step==ControlStep::Setpoint || step==ControlStep::SetHeat) &&
       (latest.action!=BootstrapAction::HeatSetpoint ||
        std::fabs(latest.heat_setpoint_c-required_target)>0.001))return false;
    if(step==ControlStep::SetOff && latest.action!=BootstrapAction::Off)
        return false;
    WiFiClient client;
    HTTPClient req;
    req.setConnectTimeout(1500);req.setTimeout(1800);
    req.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!req.begin(client,url))return false;
    req.addHeader("Content-Type","application/x-www-form-urlencoded");
    // Last cancellation point after all potentially blocking preparation.
    if(!control_armed.load() || c23_config_generation()!=config_generation ||
       (c23_mode_config().mode!=CottageMode::Hold &&
        c23_mode_config().mode!=CottageMode::Occupied)) {
        req.end();
        return false;
    }
    const int code=req.POST("");
    req.end();
    return code>=200 && code<300;
}
// Called only in the existing optimizer task, no additional RTOS stack,
// watchdog or scheduled foreground WebServer blocking.
static void cottage_control_tick() {
    control_gate.store(static_cast<int>(ControlBlock::Disabled));
    if(!control_armed.load())return;
    if(!wifi_ok.load() || !clock_ok()) {
        control_gate.store(static_cast<int>(ControlBlock::InputsUnverified));
        return;
    }
    const CottageModeConfig mode=c23_mode_config();
    const uint32_t config_generation=c23_config_generation();
    const auto sample=c23_temperature_sample();
    const int64_t now=time(nullptr);
    CN105Telemetry pump_snapshot{};
    int64_t link_utc=0;
    xSemaphoreTake(lock_data,portMAX_DELAY);
    pump_snapshot=pump;
    link_utc=cn105_link_event_utc;
    xSemaphoreGive(lock_data);
    const auto proposal=bootstrap_preview(now,sample,pump_snapshot);
    // The source-link heartbeat is necessary but not sufficient:
    // direct bridge GET and subsequent state readback are required as well.
    const bool link_fresh=sse_stream_active.load() &&
        link_utc>0 && now>=link_utc && now-link_utc<=180;
    if((mode.mode!=CottageMode::Hold && mode.mode!=CottageMode::Occupied)) {
        control_gate.store(static_cast<int>(ControlBlock::NotInHeatingMode));
        return;
    }
    if(!valid_temperature(sample,now) ||
       proposal.action==BootstrapAction::Unavailable || !link_fresh) {
        control_gate.store(static_cast<int>(ControlBlock::InputsUnverified));
        return;
    }
    // After a failed write/readback do not retry for 15 minutes.
    static uint64_t last_failed_ms=0;
    static bool failure_present=false;
    const uint64_t now_ms=static_cast<uint64_t>(esp_timer_get_time()/1000);
    if(failure_present && (now_ms<last_failed_ms ||
        now_ms-last_failed_ms<900000ULL)) {
        control_gate.store(static_cast<int>(ControlBlock::RateLimited));
        return;
    }
    bool current_heat=false;
    double current_setpoint=0;
    if(!cn105_bridge_get(current_heat,current_setpoint)) {
        control_gate.store(static_cast<int>(ControlBlock::InputsUnverified));
        return;
    }
    static bool last_command_present=false;
    static uint64_t last_command_ms=0;
    static bool mode_change_present=false;
    static uint64_t last_mode_change_ms=0;
    static bool last_bridge_heat_known=false;
    static bool last_bridge_heat=false;
    static uint64_t last_bridge_get_ms=0;
    // A new externally commanded mode or a prolonged loss of bridge
    // observations resets the minimum mode-dwell window. It is not inferred
    // from the optional Verified Snapshot event.
    const bool bridge_gap=last_bridge_heat_known &&
        (now_ms<last_bridge_get_ms ||
         now_ms-last_bridge_get_ms>180000ULL);
    if(!mode_change_present || bridge_gap ||
       (last_bridge_heat_known && last_bridge_heat!=current_heat)) {
        mode_change_present=true;
        last_mode_change_ms=now_ms;
    }
    last_bridge_heat=current_heat;
    last_bridge_heat_known=true;
    last_bridge_get_ms=now_ms;
    // Preflight GET can block: old sensor, mode and user authorization
    // snapshots must never be carried into the physical POST.
    if(!control_armed.load() ||
       config_generation!=c23_config_generation() ||
       (c23_mode_config().mode!=CottageMode::Hold &&
        c23_mode_config().mode!=CottageMode::Occupied) ||
       !valid_temperature(c23_temperature_sample(),time(nullptr))) {
        control_gate.store(static_cast<int>(ControlBlock::InputsUnverified));
        return;
    }
    ControlSnapshot candidate{};
    candidate.armed=control_armed.load();
    candidate.heating_mode=mode.mode==CottageMode::Hold ||
                        mode.mode==CottageMode::Occupied;
    candidate.temperature_fresh=valid_temperature(sample,now);
    candidate.market_fresh=proposal.action!=BootstrapAction::Unavailable;
    candidate.cn105_link_fresh=link_fresh;
    candidate.bridge_get_ok=true; // separate live ESPHome GET succeeded
    candidate.current_heat=current_heat;
    candidate.current_setpoint_c=current_setpoint;
    candidate.now_ms=now_ms;
    candidate.has_last_command=last_command_present;
    candidate.last_command_ms=last_command_ms;
    candidate.has_confirmed_mode_change=mode_change_present;
    candidate.last_mode_change_ms=last_mode_change_ms;
    candidate.proposal=proposal;
    const auto decision=choose_mitsubishi_control(candidate);
    control_gate.store(static_cast<int>(decision.gate));
    if(decision.step==ControlStep::None)return;
    MitsubishiHttpRequest request{};
    if(decision.step==ControlStep::Setpoint)
        request=mitsubishi_setpoint_request(decision.target_c);
    else request=mitsubishi_mode_request(
        decision.step==ControlStep::SetHeat?MitsubishiMode::Heat:
                                            MitsubishiMode::Off);
    if(!request.valid || !control_armed.load())return;
    // Timestamp from the attempted command, regardless of HTTP status;
    // prevents retry floods on abrupt gateway or LAN failures.
    last_command_present=true;
    last_command_ms=now_ms;
    control_last_step.store(static_cast<int>(decision.step));
    control_last_command_utc.store(now);
    control_commands_sent.fetch_add(1);
    // Record any mode POST attempt as a potentially applied transition,
    // including cases where the network reply or subsequent readback fails.
    if(decision.step!=ControlStep::Setpoint) {
        mode_change_present=true;
        last_mode_change_ms=now_ms;
    }
    const bool sent=cn105_bridge_post(request,config_generation,
                                      decision.step,decision.target_c);
    // ESPHome may acknowledge the POST before updating its REST state.
    // Confirm only what the bridge GET reports, never claim UART proof.
    bool confirmed=false;
    if(sent) {
        for(unsigned attempt=0;attempt<5 && control_armed.load();++attempt) {
            vTaskDelay(pdMS_TO_TICKS(600));
            bool read_heating=false;
            double read_setpoint=0;
            if(!cn105_bridge_get(read_heating,read_setpoint))continue;
            confirmed=decision.step==ControlStep::Setpoint ?
                std::fabs(read_setpoint-decision.target_c)<=0.001 :
                decision.step==ControlStep::SetHeat ? read_heating :
                !read_heating;
            if(confirmed)break;
        }
    }
    if(confirmed) {
        control_commands_confirmed.fetch_add(1);
        control_last_ack_utc.store(time(nullptr));
        failure_present=false;
        if(decision.step!=ControlStep::Setpoint) {
            last_mode_change_ms=now_ms;
            mode_change_present=true;
        }
    } else {
        control_commands_failed.fetch_add(1);
        last_failed_ms=now_ms;
        failure_present=true;
    }
}
static void cottage_control_registration() {
    // Every boot and every OTA update starts DISARMED. A previously saved
    // arm must not silently resume physical writes after reboot.
    control_armed.store(false);
    Preferences prefs;
    if(prefs.begin("cottage_ctl",false)) {
        if(prefs.getBool("armed",false))prefs.putBool("armed",false);
        prefs.end();
    }
    web.on("/api/control/arm",HTTP_POST,[]() {
        if(!web.hasArg("token") ||
           !c23_authorize_local_setting(web.arg("token").c_str())) {
            web.send(403,"application/json","{\"saved\":false}");
            return;
        }
        if(!web.hasArg("enabled") ||
           (web.arg("enabled")!="true"&&web.arg("enabled")!="false")) {
            web.send(400,"application/json","{\"saved\":false}");
            return;
        }
        const bool requested=web.arg("enabled")=="true";
        Preferences p;
        bool stored=false;
        if(p.begin("cottage_ctl",false)) {
            stored=p.putBool("armed",requested)>0;
            p.end();
        }
        if(stored)control_armed.store(requested);
        web.send(stored?200:500,"application/json",
            stored?"{\"saved\":true}":"{\"saved\":false}");
    });
}
// Use 64-bit monotonic ESP timer for an actual deadline (not post-hoc wall time).
static uint64_t cottage_now_ms(void*) {
    return static_cast<uint64_t>(esp_timer_get_time()/1000);
}
static bool cottage_cancel(void*) {
    return !wifi_ok.load() ||
        static_cast<uint32_t>(millis()-web_loop_heartbeat_ms.load())>5000u;
}
static void optimizer_worker(void*) {
    auto* prices=new enevina_firmware::PricePoint[200];
    auto* weather=new enevina::OutdoorPoint[64];
    auto* aligned=new Spot15[200];
    auto* bridge=new (std::nothrow) BridgeResult;
    auto* rows=new (std::nothrow) Row[max_horizon_slots];
    for(;;) {
        const int64_t now=time(nullptr);
        if(wifi_ok.load() && clock_ok() && prices && weather && aligned && bridge && rows) {
            CN105Telemetry cp;TemperatureSample ts;
            unsigned observed_mode_minutes=0;
            uint32_t optimizer_mode_generation=0;
            xSemaphoreTake(lock_data,portMAX_DELAY);
            cp=pump;
            optimizer_mode_generation=observed_mode_generation;
            observed_mode_minutes=observed_mode_duration.minutes(
                cp.mode,static_cast<uint64_t>(esp_timer_get_time()/1000),
                valid_cn105(cp,now));
            xSemaphoreGive(lock_data);
            ts=c23_temperature_sample();
            const auto ms=enevina_firmware::market_status();
            const auto ws=enevina_firmware::weather_status();
            const size_t pc=enevina_firmware::future_prices(prices,200);
            const size_t wc=enevina_firmware::future_weather(weather,64);
            for(size_t i=0;i<pc;i++) aligned[i]={prices[i].start_utc,
                prices[i].spot_eur_per_kwh};
            *bridge=BridgeResult{};
            // A fresh indoor measurement is not a forecast for an entire
            // unfinished quarter-hour. Only optimize near the UTC boundary.
            const bool near_slot_start=(now%900<=60);
            if(near_slot_start && pc&&wc) {
                *bridge=join_forecast_two_tariffs(now,ms.last_fetch_utc,ws.fetched_utc,
                    aligned,pc,weather,wc,tariff(false),tariff(true),192);
                if(bridge->status!=BridgeStatus::Ready)
                    *bridge=join_forecast_two_tariffs(now,ms.last_fetch_utc,ws.fetched_utc,
                        aligned,pc,weather,wc,tariff(false),tariff(true),96);
            }
            xSemaphoreTake(lock_data,portMAX_DELAY);
            if(near_slot_start) bridge_status=bridge->status;
            xSemaphoreGive(lock_data);
            if(near_slot_start && bridge->status==BridgeStatus::Ready &&
               valid_cn105(cp,now) && valid_temperature(ts,now) &&
               (cp.mode==ObservedPumpMode::Off ||
                cp.mode==ObservedPumpMode::Heat) &&
               COTTAGE_SYNTHETIC_TEST_MODEL && valid(model)) {
                const State initial{ts.indoor_c,
                    cp.mode==ObservedPumpMode::Heat,observed_mode_minutes};
                const uint32_t begun=millis();
                MpcRunControls limits{};
                limits.runtime_budget_ms=1800;
                limits.monotonic_now_ms=cottage_now_ms;
                limits.cancel_requested=cottage_cancel;
                const MpcResult result=optimize(model,initial,bridge->periods.data(),
                    bridge->count,3*1024*1024,limits);
                const uint32_t elapsed=millis()-begun;
                optimizer_ms=elapsed;max_optimizer_ms=std::max(max_optimizer_ms,elapsed);
                std::fill(rows,rows+max_horizon_slots,Row{});
                State state=initial;
                if(result.status==MpcStatus::Ok) {
                    for(size_t i=0;i<result.count;i++) {
                        const bool heat16=result.heat_16_c[i]!=0;
                        const auto step=advance(model,state,bridge->periods[i],heat16);
                        state=step.next;
                        rows[i]={bridge->first_utc+static_cast<int64_t>(900*i),
                            state.indoor_c,step.electric_kwh,step.cost_eur,heat16};
                    }
                }
                xSemaphoreTake(lock_data,portMAX_DELAY);
                // The optimization ran without holding lock_data. An SSE
                // mode transition may have invalidated its input snapshot
                // while the solver was busy; never publish that old plan,
                // even if HEAT -> OFF -> HEAT returned to the same mode.
                const bool mode_changed=optimizer_mode_generation!=
                    observed_mode_generation || cp.mode!=pump.mode;
                last_optimized_utc=now;
                if(mode_changed) {
                    plan_count=0;
                    last_plan.status=MpcStatus::InvalidInput;
                } else if(result.status==MpcStatus::Ok && elapsed<=2000) {
                    last_plan=result;
                    std::copy(rows,rows+max_horizon_slots,plan_rows.begin());
                    plan_count=result.count;
                } else {
                    plan_count=0;
                    last_plan.status=MpcStatus::ResourceLimit;
                }
                strlcpy(reason_text,mode_changed?
                    "CN105 tila vaihtui laskennan aikana":
                    result.status==MpcStatus::Ok && elapsed<=2000?
                    "Vain synteettinen testimalli":"MPC infeasible / virhe",
                    sizeof(reason_text));
                xSemaphoreGive(lock_data);
            } else if(near_slot_start) {
                set_reason(!COTTAGE_SYNTHETIC_TEST_MODEL?
                    "Rakennusmalli ei ole kalibroitu":
                    !valid_temperature(ts,now)?"Riippumaton anturi puuttuu / vanha":
                    !valid_cn105(cp,now)?"CN105 vanha tai puuttuu":
                    cp.mode!=ObservedPumpMode::Off &&
                    cp.mode!=ObservedPumpMode::Heat?"CN105 tila ei tue optimointia":
                    "Ennuste puuttuu / vanhentunut");
                xSemaphoreTake(lock_data,portMAX_DELAY);
                plan_count=0;last_plan.status=MpcStatus::InvalidInput;
                xSemaphoreGive(lock_data);
            }
            if(!valid_cn105(cp,now) || !valid_temperature(ts,now) ||
               (cp.mode!=ObservedPumpMode::Heat && cp.mode!=ObservedPumpMode::Off) ||
               !ms.current_price_valid || !ws.available) {
                xSemaphoreTake(lock_data,portMAX_DELAY);
                plan_count=0;
                last_plan.status=MpcStatus::InvalidInput;
                xSemaphoreGive(lock_data);
            }
            Health health;
            health.now_utc=now;
            health.pump=cp;health.independent_temperature=ts;
            health.watchdog_ok=(millis()-web_loop_heartbeat_ms.load()<5000);
            health.calibrated=false;
            health.independent_freeze_protection_verified=false;
            health.price_fresh=ms.current_price_valid;
            health.weather_fresh=ws.available;
            health.mpc_ready=false;
            health.plan=last_plan;
            xSemaphoreTake(lock_data,portMAX_DELAY);safety=supervise(model,health);
            free_heap=ESP.getFreeHeap();free_psram=ESP.getFreePsram();
            stack_watermark=uxTaskGetStackHighWaterMark(nullptr);
            xSemaphoreGive(lock_data);
        } else {
            xSemaphoreTake(lock_data,portMAX_DELAY);
            plan_count=0;
            last_plan.status=MpcStatus::InvalidInput;
            strlcpy(reason_text,"Wi-Fi, aika tai resurssit eivät ole valmiina",
                    sizeof(reason_text));
            xSemaphoreGive(lock_data);
        }
        cottage_control_tick();
        // Recompute on quarter-hour boundary, retries every minute when offline.
        const time_t t=time(nullptr);
        int sleep_s=clock_ok()?static_cast<int>(900-(t%900)):60;
        if(sleep_s>60)sleep_s=60; // Allows freshness monitoring during long slots.
        vTaskDelay(pdMS_TO_TICKS(std::max(5,sleep_s)*1000));
    }
}
static void status_endpoint() {
    CN105Telemetry cp;ShellyTelemetry st;TemperatureSample te;
    CN105Telemetry observed; // SSE-only values, never source-verified
    BridgeStatus bs;MpcResult plan;Decision current_safety;
    int64_t optimized=0,climate_time=0,compressor_time=0,link_time=0;
    double link_uptime_s=0;
    bool relay_on=false;
    char reason[80];
    uint32_t ms,worst,heap,psram,stack;
    xSemaphoreTake(lock_data,portMAX_DELAY);
    cp=pump;observed=sse_observed_pump;
    st=consumption;/* C2.3 temperature read outside lock */bs=bridge_status;
    relay_on=shelly_relay_on;
    current_safety=safety;
    climate_time=climate_utc;
    compressor_time=compressor_utc;
    link_time=cn105_link_event_utc;
    link_uptime_s=cn105_link_uptime_s;
    plan=last_plan;optimized=last_optimized_utc;
    strlcpy(reason,reason_text,sizeof(reason));
    ms=optimizer_ms;worst=max_optimizer_ms;heap=free_heap;psram=free_psram;
    stack=stack_watermark;
    xSemaphoreGive(lock_data);
    te=c23_temperature_sample();
    const int64_t now=time(nullptr);
    const auto price=enevina_firmware::market_status();
    const auto weather=enevina_firmware::weather_status();
    double total=0;
    struct tm local{};
    time_t ts=static_cast<time_t>(now);
    localtime_r(&ts,&local);
    enevina::Tariff t=tariff(local.tm_hour<7||local.tm_hour>=22);
    t.spot_eur_per_kwh=price.current_spot_eur_per_kwh;
    const bool valid_total=price.current_price_valid &&
        enevina::calculate_total_price(t,total);
    const BootstrapDecision bootstrap=bootstrap_preview(now,te,cp);
    cJSON* root=cJSON_CreateObject();
    cJSON_AddStringToObject(root,"mode",control_armed.load()?
        "CN105_AUTOMAATIO_KAYTOSSA":"VARJOTILA_EI_KOMENTOJA");
    cJSON_AddBoolToObject(root,"physical_writes",control_armed.load());
    cJSON_AddBoolToObject(root,"control_armed",control_armed.load());
    cJSON_AddStringToObject(root,"control_gate",
        control_gate_name(static_cast<ControlBlock>(control_gate.load())));
    cJSON_AddStringToObject(root,"control_last_step",
        control_step_name(static_cast<ControlStep>(control_last_step.load())));
    cJSON_AddNumberToObject(root,"control_commands_sent",control_commands_sent.load());
    cJSON_AddNumberToObject(root,"control_commands_confirmed",control_commands_confirmed.load());
    cJSON_AddNumberToObject(root,"control_commands_failed",control_commands_failed.load());
    cJSON_AddNumberToObject(root,"control_last_command_utc",
                            static_cast<double>(control_last_command_utc.load()));
    cJSON_AddNumberToObject(root,"control_last_ack_utc",
                            static_cast<double>(control_last_ack_utc.load()));
    cJSON_AddNumberToObject(root,"control_last_bridge_read_utc",
                            static_cast<double>(control_last_bridge_read_utc.load()));
    cJSON_AddStringToObject(root,"bootstrap_action",
                             bootstrap_action_name(bootstrap.action));
    cJSON_AddStringToObject(root,"bootstrap_reason",
                             bootstrap_reason_name(bootstrap.reason));
    cJSON_AddBoolToObject(root,"bootstrap_executable",false);
    if(bootstrap.requested_room_c>0)
        cJSON_AddNumberToObject(root,"bootstrap_room_target_c",
                                bootstrap.requested_room_c);
    else cJSON_AddNullToObject(root,"bootstrap_room_target_c");
    // Transport path can be inspected before F08 activation. This is a
    // string preview only; no network POST exists in the Lite runtime.
    if(bootstrap.action==BootstrapAction::HeatSetpoint) {
        cJSON_AddNumberToObject(root,"bootstrap_heat_setpoint_c",
                                bootstrap.heat_setpoint_c);
        const auto request=mitsubishi_setpoint_request(
            bootstrap.heat_setpoint_c);
        if(request.valid)
            cJSON_AddStringToObject(root,"bootstrap_mitsubishi_command_preview",
                                    request.path.data());
        else cJSON_AddNullToObject(root,"bootstrap_mitsubishi_command_preview");
    } else {
        cJSON_AddNullToObject(root,"bootstrap_heat_setpoint_c");
        cJSON_AddNullToObject(root,"bootstrap_mitsubishi_command_preview");
    }
    if(bootstrap.action!=BootstrapAction::Unavailable) {
        cJSON_AddNumberToObject(root,"bootstrap_total_now",
                                bootstrap.current_total_eur_per_kwh);
        cJSON_AddNumberToObject(root,"bootstrap_median_2h",
                                bootstrap.compared_median_eur_per_kwh);
    } else {
        cJSON_AddNullToObject(root,"bootstrap_total_now");
        cJSON_AddNullToObject(root,"bootstrap_median_2h");
    }
    const auto pump_quality=enevina::assess_measurement(cp.evidence,now,180);
    const bool verified_uart=valid_cn105(cp,now) &&
        cp.evidence.source_time_verified;
    const bool observed_climate=climate_time>0 && now>=climate_time;
    const bool observed_compressor=compressor_time>0 && now>=compressor_time;
    const bool sse_online=sse_stream_active.load();
    cJSON_AddBoolToObject(root,"cn105",verified_uart);
    // An ESPHome /events snapshot is useful *observed* telemetry. Its
    // receive timestamp is NOT a proof of a new UART pump poll.
    cJSON_AddBoolToObject(root,"cn105_source_verified",verified_uart);
    cJSON_AddStringToObject(root,"cn105_measurement_quality",verified_uart?
        enevina::quality_name(pump_quality):observed_climate?
        "SSE_SNAPSHOT_UART_UNVERIFIED":"UNVERIFIED_SOURCE");
    cJSON_AddBoolToObject(root,"cn105_sse_climate_received",observed_climate);
    cJSON_AddBoolToObject(root,"cn105_sse_compressor_received",observed_compressor);
    cJSON_AddBoolToObject(root,"cn105_sse_climate_recent",
        observed_climate && sse_online && now-climate_time<=180);
    cJSON_AddBoolToObject(root,"cn105_sse_compressor_recent",
        observed_compressor && sse_online && now-compressor_time<=180);
    if(observed_climate) {
        cJSON_AddNumberToObject(root,"cn105_sse_climate_age_s",
                                static_cast<double>(now-climate_time));
        cJSON_AddNumberToObject(root,"cn105_sse_indoor_c",observed.indoor_c);
        cJSON_AddNumberToObject(root,"cn105_sse_setpoint_c",observed.setpoint_c);
        cJSON_AddStringToObject(root,"cn105_sse_mode",
            observed.mode==ObservedPumpMode::Heat?"HEAT":
            observed.mode==ObservedPumpMode::Off?"OFF":"OTHER");
    } else {
        cJSON_AddNullToObject(root,"cn105_sse_climate_age_s");
        cJSON_AddNullToObject(root,"cn105_sse_indoor_c");
        cJSON_AddNullToObject(root,"cn105_sse_setpoint_c");
        cJSON_AddNullToObject(root,"cn105_sse_mode");
    }
    if(observed_compressor) {
        cJSON_AddNumberToObject(root,"cn105_sse_compressor_age_s",
                                static_cast<double>(now-compressor_time));
        cJSON_AddNumberToObject(root,"cn105_sse_compressor_hz",
                                observed.compressor_hz);
    } else {
        cJSON_AddNullToObject(root,"cn105_sse_compressor_age_s");
        cJSON_AddNullToObject(root,"cn105_sse_compressor_hz");
    }
    cJSON_AddStringToObject(root,"pump_source_quality",
                            enevina::quality_name(pump_quality));
    cJSON_AddBoolToObject(root,"pump_source_time_verified",
                          cp.evidence.source_time_verified);
    cJSON_AddNumberToObject(root,"pump_received_utc",
                            static_cast<double>(cp.evidence.received_utc));
    if(cp.evidence.source_time_verified && cp.evidence.source_utc>0)
        cJSON_AddNumberToObject(root,"pump_source_utc",
                                static_cast<double>(cp.evidence.source_utc));
    else cJSON_AddNullToObject(root,"pump_source_utc");
    cJSON_AddStringToObject(root,"pump_mode",!valid_cn105(cp,now)?
        "EI VAHVISTETTU":cp.mode==ObservedPumpMode::Heat?
        "HEAT":cp.mode==ObservedPumpMode::Off?"OFF":"MUU/EI");
    if(valid_cn105(cp,now)) {
        cJSON_AddNumberToObject(root,"pump_indoor_c",cp.indoor_c);
        cJSON_AddNumberToObject(root,"compressor_hz",cp.compressor_hz);
    } else {
        cJSON_AddNullToObject(root,"pump_indoor_c");
        cJSON_AddNullToObject(root,"compressor_hz");
    }
    if(valid_temperature(te,now))cJSON_AddNumberToObject(root,"independent_c",te.indoor_c);
    else cJSON_AddNullToObject(root,"independent_c");
    cJSON_AddBoolToObject(root,"shelly_relay_on",relay_on);
    if(valid_shelly(st,now))cJSON_AddNumberToObject(root,"shelly_w",st.watts);
    else cJSON_AddNullToObject(root,"shelly_w");
    if(price.current_price_valid)cJSON_AddNumberToObject(root,"spot",price.current_spot_eur_per_kwh);
    else cJSON_AddNullToObject(root,"spot");
    if(weather.available)cJSON_AddNumberToObject(root,"weather_c",weather.next_temperature_c);
    else cJSON_AddNullToObject(root,"weather_c");
    if(valid_total)cJSON_AddNumberToObject(root,"total_price",total);
    else cJSON_AddNullToObject(root,"total_price");
    cJSON_AddNumberToObject(root,"bridge_status",static_cast<int>(bs));
    cJSON_AddNumberToObject(root,"mpc_status",static_cast<int>(plan.status));
    cJSON_AddNumberToObject(root,"mpc_ms",ms);
    cJSON_AddNumberToObject(root,"mpc_worst_ms",worst);
    cJSON_AddNumberToObject(root,"optimized_utc",static_cast<double>(optimized));
    cJSON_AddNumberToObject(root,"free_heap",heap);
    cJSON_AddNumberToObject(root,"free_psram",
                            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root,"stack_free_bytes",stack);
    cJSON_AddStringToObject(root,"reason",reason);
    cJSON_AddNumberToObject(root,"bad_reads",failed_reads.load());
    cJSON_AddBoolToObject(root,"model_calibrated",false);
    cJSON_AddBoolToObject(root,"independent_freeze_protection_verified",false);
    cJSON_AddNumberToObject(root,"safety_mode",static_cast<int>(current_safety.mode));
    cJSON_AddNumberToObject(root,"safety_reason",static_cast<int>(current_safety.reason));
    cJSON_AddBoolToObject(root,"sse_connected",sse_stream_active.load());
    cJSON_AddNumberToObject(root,"sse_http_status",sse_last_http_status.load());
    cJSON_AddNumberToObject(root,"sse_connections",sse_connections.load());
    cJSON_AddNumberToObject(root,"sse_disconnects",sse_disconnects.load());
    cJSON_AddNumberToObject(root,"sse_climate_events",sse_climate_events.load());
    cJSON_AddNumberToObject(root,"sse_compressor_events",sse_compressor_events.load());
    cJSON_AddNumberToObject(root,"sse_link_events",sse_link_events.load());
    cJSON_AddNumberToObject(root,"cn105_proof_state",cn105_source_proof_status.load());
    cJSON_AddNumberToObject(root,"cn105_proven_uart_polls",cn105_source_proof_good.load());
    // Device link uptime cannot make a stale climate/compressor reading fresh.
    cJSON_AddNumberToObject(root,"cn105_climate_utc",static_cast<double>(climate_time));
    cJSON_AddNumberToObject(root,"cn105_compressor_utc",static_cast<double>(compressor_time));
    cJSON_AddNumberToObject(root,"cn105_link_event_utc",static_cast<double>(link_time));
    cJSON_AddNumberToObject(root,"cn105_link_uptime_s",link_uptime_s);
    cJSON_AddNumberToObject(root,"shelly_last_read_utc",static_cast<double>(st.captured_utc));
    cJSON_AddNumberToObject(root,"shelly_total_wh",st.total_wh);
    cJSON_AddNumberToObject(root,"shelly_reads_ok",shelly_reads_ok.load());
    cJSON_AddNumberToObject(root,"shelly_reads_bad",shelly_reads_bad.load());
    cJSON_AddNumberToObject(root,"shelly_rtt_ms",shelly_rtt_ms.load());
    cJSON_AddNumberToObject(root,"market_last_fetch_utc",
                            static_cast<double>(price.last_fetch_utc));
    cJSON_AddNumberToObject(root,"market_slots",price.slots);
    cJSON_AddNumberToObject(root,"weather_last_fetch_utc",
                            static_cast<double>(weather.fetched_utc));
    cJSON_AddNumberToObject(root,"weather_points",weather.points);
    cJSON_AddNumberToObject(root,"uptime_ms",millis());
    cJSON_AddNumberToObject(root,"boot_reset_reason_code",boot_reset_reason_code);
    cJSON_AddStringToObject(root,"firmware_version","c2.4-dev");
    const esp_partition_t* active=esp_ota_get_running_partition();
    cJSON_AddStringToObject(root,"ota_partition",active?active->label:"unknown");
    esp_ota_img_states_t image_state=ESP_OTA_IMG_UNDEFINED;
    const bool has_ota_state=active &&
        esp_ota_get_state_partition(active,&image_state)==ESP_OK;
    cJSON_AddNumberToObject(root,"ota_image_state",
                            has_ota_state?static_cast<int>(image_state):-1);
    cJSON_AddBoolToObject(root,"ota_boot_confirmed",ota_boot_confirmed);
    cJSON_AddBoolToObject(root,"ota_upload_enabled",false);
    cJSON_AddBoolToObject(root,"ota_signed_pull_enabled",cottage_ota_configured());
    cJSON_AddNumberToObject(root,"ota_signed_good_version",cottage_ota_good_version());
    cJSON_AddNumberToObject(root,"ota_signed_candidate",cottage_ota_last_candidate());
    cJSON_AddNumberToObject(root,"ota_signed_status",cottage_ota_status());
    cJSON_AddNumberToObject(root,"free_internal_heap",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root,"min_free_internal_heap",
                            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root,"min_free_psram",
                            heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root,"largest_free_internal_heap",
                            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(root,"network_budget_denials",
                            enevina_firmware::network_budget_denials());
    cJSON_AddBoolToObject(root,"ble_initialized",c23_ble_initialized());
    cJSON_AddNumberToObject(root,"observed_min_largest_internal",
                            enevina_firmware::network_budget_min_largest_internal());
    cJSON_AddNumberToObject(root,"sse_stack_free",
                            task_sse?uxTaskGetStackHighWaterMark(task_sse):0);
    cJSON_AddNumberToObject(root,"shelly_stack_free",
                            task_shelly?uxTaskGetStackHighWaterMark(task_shelly):0);
    cJSON_AddNumberToObject(root,"ruuvi_stack_free",
                            task_ruuvi?uxTaskGetStackHighWaterMark(task_ruuvi):0);
    cJSON_AddNumberToObject(root,"optimizer_stack_free",
                            task_optimizer?uxTaskGetStackHighWaterMark(task_optimizer):0);
    if(WiFi.status()==WL_CONNECTED)cJSON_AddNumberToObject(root,"wifi_rssi_dbm",WiFi.RSSI());
    else cJSON_AddNullToObject(root,"wifi_rssi_dbm");
    c23_append_status(root);
    char* serialized=cJSON_PrintUnformatted(root);
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8",serialized?serialized:"{}");
    cJSON_free(serialized);cJSON_Delete(root);
}
static void plan_endpoint() {
    xSemaphoreTake(lock_data,portMAX_DELAY);
    const MpcResult p=last_plan;
    const size_t n=plan_count;
    const int64_t computed_utc=last_optimized_utc;
    const CN105Telemetry cp=pump;
    // The independent Ruuvi sample is acquired outside the main mutex.
    std::unique_ptr<Row[]> rows(new(std::nothrow) Row[max_horizon_slots]);
    if(rows) std::copy(plan_rows.begin(),plan_rows.end(),rows.get());
    xSemaphoreGive(lock_data);
    const TemperatureSample ts=c23_temperature_sample();
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8","");
    char buf[220];
    const int64_t current_utc=time(nullptr);
    const bool ok=rows && p.status==MpcStatus::Ok && n>0 && n<=192 &&
        current_utc>=computed_utc && current_utc-computed_utc<900 &&
        valid_cn105(cp,current_utc) && valid_temperature(ts,current_utc) &&
        (cp.mode==ObservedPumpMode::Heat || cp.mode==ObservedPumpMode::Off) &&
        COTTAGE_SYNTHETIC_TEST_MODEL;
    snprintf(buf,sizeof(buf),
      "{\"valid\":%s,\"count\":%u,\"cost_eur\":%.8f,\"minimum_c\":%.5f,\"slots\":[",
      ok?"true":"false",static_cast<unsigned>(ok?n:0),ok?p.cost_eur:0,ok?p.minimum_c:0);
    web.sendContent(buf);
    for(size_t i=0;ok&&i<n;i++) {
        const auto& r=rows[i];
        snprintf(buf,sizeof(buf),"%s{\"utc\":%lld,\"heat\":%s,\"indoor_c\":%.5f,\"kwh\":%.6f,\"eur\":%.7f}",
            i?",":"",static_cast<long long>(r.utc),r.heat16?"true":"false",
            r.indoor,r.electric,r.price);
        web.sendContent(buf);
    }
    web.sendContent("]}");
    web.sendContent("");
}
// C2.5: Small, read-only 15-minute price feed for the local premium dashboard.
// Do not manufacture price points when the clock or market data is stale.
static void prices_endpoint() {
    const auto market=enevina_firmware::market_status();
    if(!market.clock_trusted || !market.current_price_valid) {
        web.sendHeader("Cache-Control","no-store");
        web.send(200,"application/json; charset=utf-8",
                 "{\"valid\":false,\"slots\":[]}");
        return;
    }
    constexpr size_t kMaxPriceSlots=192;
    auto* points=static_cast<enevina_firmware::PricePoint*>(
        heap_caps_malloc(sizeof(enevina_firmware::PricePoint)*kMaxPriceSlots,
                         MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!points) {
        web.send(503,"application/json; charset=utf-8",
                 "{\"valid\":false,\"slots\":[]}");
        return;
    }
    const size_t count=enevina_firmware::future_prices(points,kMaxPriceSlots);
    web.sendHeader("Cache-Control","no-store");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200,"application/json; charset=utf-8","");
    web.sendContent(count?"{\"valid\":true,\"slots\":[":"{\"valid\":false,\"slots\":[");
    char line[192];
    size_t emitted=0;
    for(size_t i=0;i<count;++i) {
        const auto& price=points[i];
        if(!std::isfinite(price.spot_eur_per_kwh)) continue;
        const time_t utc=static_cast<time_t>(price.start_utc);
        struct tm local{};
        localtime_r(&utc,&local); // Finland local day/night tariff, including DST
        enevina::Tariff t=tariff(local.tm_hour<7||local.tm_hour>=22);
        t.spot_eur_per_kwh=price.spot_eur_per_kwh;
        double total=0;
        const bool has_total=enevina::calculate_total_price(t,total) &&
                             std::isfinite(total);
        char total_buf[36];
        if(has_total) snprintf(total_buf,sizeof(total_buf),"%.8f",total);
        else strlcpy(total_buf,"null",sizeof(total_buf));
        snprintf(line,sizeof(line),
            "%s{\"utc\":%lld,\"spot\":%.8f,\"total\":%s}",
            emitted++?",":"",static_cast<long long>(price.start_utc),
            price.spot_eur_per_kwh,total_buf);
        web.sendContent(line);
        if(i%16==15) delay(1);
    }
    web.sendContent("]}");
    web.sendContent("");
    heap_caps_free(points);
}
static void setup_model() {
    // Explicit test-only values. Real cottage data are not yet calibrated.
    if(!COTTAGE_SYNTHETIC_TEST_MODEL) return;
    model.thermal_mass_kwh_per_c=2.0;
    model.loss_kw_per_c=0.12;
    model.heat_kw_at_zero_c=3.2;
    model.heat_kw_change_per_outdoor_c=0.03;
    model.minimum_heat_kw=1.0;
    model.cop_at_zero_c=2.8;
    model.cop_change_per_outdoor_c=0.04;
    model.minimum_cop=1.2;model.maximum_cop=5.0;
    model.startup_delay_minutes=3.0;
    model.startup_electric_kwh=0.05;
    model.heat_standby_kw=0.012;
    model.off_standby_kw=0.002;
    model.planning_min_c=8.0;model.critical_c=5.0;
    model.maximum_indoor_c=14.5;
    model.min_heat_minutes=60;
    model.min_off_minutes=60;
    model.switching_penalty_eur=0.03;
}
void setup() {
    Serial.begin(115200);
    boot_reset_reason_code=static_cast<int>(esp_reset_reason());
    Serial.printf("ENEVINA_LITE_BOOT reset_reason_code=%d\n",boot_reset_reason_code);
    lock_data=xSemaphoreCreateMutex();
    if(!lock_data) abort();
    if(ESP.getPsramSize()<7*1024*1024) {
        // Board calibration and firmware memory setup differ.
        // This is a test-card firmware; refuse to start live readers.
        Serial.println("C2_SHADOW_FATAL: expected >=7MB PSRAM unavailable");
        while(true) { delay(1000); }
    }
    if(!enevina_firmware::network_budget_init()) {
        Serial.println("C24_FATAL: memory guard allocation failed");
        while(true) delay(1000);
    }
    cJSON_Hooks hooks{};
    hooks.malloc_fn=json_alloc_psram;
    hooks.free_fn=json_free_psram;
    cJSON_InitHooks(&hooks); // Once, before any reader/task starts.
    setup_model();
    c23_initialize(); // NVS mode + persistent explicit Ruuvi bonding
    cottage_ota_initialize(); // Persist successful signed release + reject failed candidate
    // Avoid device contact without explicit local configuration.
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    if(strlen(COTTAGE_WIFI_SSID)) WiFi.begin(COTTAGE_WIFI_SSID,COTTAGE_WIFI_PASSWORD);
    setenv("TZ","EET-2EEST,M3.5.0/3,M10.5.0/4",1);
    tzset();
    configTime(0,0,"pool.ntp.org","time.google.com");
    web.on("/",HTTP_GET,[](){
        web.sendHeader("Cache-Control","no-store");
        web.sendHeader("X-Content-Type-Options","nosniff");
        web.sendHeader("Content-Security-Policy",
            "default-src 'self'; connect-src 'self'; img-src 'self' data:; "
            "script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
            "frame-ancestors 'none'; base-uri 'none'");
        web.send_P(200,"text/html; charset=utf-8",premium_dashboard_html);
    });
    web.on("/api/status",HTTP_GET,status_endpoint);
    web.on("/api/plan",HTTP_GET,plan_endpoint);
    web.on("/api/prices",HTTP_GET,prices_endpoint);
    c23_register_routes(web);
    cottage_control_registration(); // explicit local arming; defaults OFF
    web.onNotFound([](){web.send(404,"text/plain","Ei löydy");});
    web.begin();
    boot_ui_started=true;
    boot_validation_started_ms=millis();
    enableLoopWDT(); // Arduino ESP32 loop-task hardware watchdog.
    // Initialize BLE before starting memory-heavy HTTPS workers.
    xTaskCreatePinnedToCore(c23_ble_worker,"cottage_ruuvi",10240,nullptr,1,&task_ruuvi,0);
    for(int i=0;i<150 && !c23_ble_initialized();++i)
        delay(20);
    if(!c23_ble_initialized())
        Serial.println("C24_WARN: BLE initialization not confirmed");
    enevina_firmware::start_market_reader();
    enevina_firmware::start_weather_reader();
    xTaskCreatePinnedToCore(sse_worker,"cottage_cn105",8192,nullptr,2,&task_sse,0);
    xTaskCreatePinnedToCore(shelly_worker,"cottage_shelly",6144,nullptr,2,&task_shelly,0);
    // BLE already running. CN105 writes are gated by explicit local arming.
    xTaskCreatePinnedToCore(optimizer_worker,"cottage_optimizer",16384,nullptr,1,&task_optimizer,1);
    if(cottage_ota_configured() &&
        xTaskCreatePinnedToCore(cottage_ota_worker,"cottage_ota",
                                16384,nullptr,1,&task_ota,1)!=pdPASS) {
        task_ota=nullptr;
        Serial.println("C24_OTA_ERROR: signed OTA task creation failed");
    }
    Serial.println("ENEVINA LITE: optional CN105 controller, default DISARMED");
}
void loop() {
    wifi_ok.store(WiFi.status()==WL_CONNECTED);
    web_loop_heartbeat_ms.store(millis());
    web.handleClient();
    verify_new_ota_boot();
    c23_tick(); // local BLE/mode/alarm history works with wifi=0
    // Serial evidence is useful during fully isolated cold-boot tests.
    // Never print access tokens, Wi-Fi passwords or raw device addresses.
    static uint32_t last_serial_ms=0;
    if(static_cast<uint32_t>(millis()-last_serial_ms)>=30000u) {
        last_serial_ms=millis();
        CN105Telemetry cp;ShellyTelemetry sh;TemperatureSample temp;
        size_t count=0;
        xSemaphoreTake(lock_data,portMAX_DELAY);
        cp=pump;sh=consumption;count=plan_count;
        xSemaphoreGive(lock_data);
        temp=c23_temperature_sample();
        const int64_t now=time(nullptr);
        Serial.printf("C2_SHADOW wifi=%d clock=%d cn105=%d shelly=%d ble=%d "
            "plan_slots=%u heap=%u psram=%u psram_total=%u internal_low=%u bad_reads=%u\n",
            static_cast<int>(wifi_ok.load()),static_cast<int>(clock_ok()),
            static_cast<int>(valid_cn105(cp,now)),
            static_cast<int>(valid_shelly(sh,now)),
            static_cast<int>(valid_temperature(temp,now)),
            static_cast<unsigned>(count),ESP.getFreeHeap(),ESP.getFreePsram(),
            ESP.getPsramSize(),heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
            static_cast<unsigned>(failed_reads.load()));
    }
    delay(15);
}

#endif // COTTAGE_BENCH_MODE
