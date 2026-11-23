#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <nvs.h>
#include "cottage/nvs_cache_reclaim_policy.hpp"
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
#include "cottage_site_settings.hpp"
#include "cottage_midea_service.hpp"
#include "cottage_gree_service.hpp"
#include "cottage_daikin_service.hpp"
#include "cottage_daikin_dsiot_service.hpp"
#include "cottage_cn105_direct_service.hpp"
#include "cottage_daikin_s21_service.hpp"
#include "cottage_haier_service.hpp"
#include "cottage_panasonic_service.hpp"
#include "cottage_wf_rac_service.hpp"
#include "pump/adapter_catalog.hpp"
#include "cottage/control_intent.hpp"
#include "cottage_wifi.hpp"
#include "cottage/physics.hpp"
#include "cottage/mpc.hpp"
#include "cottage/telemetry.hpp"
#include "cottage/supervisor.hpp"
#include "cottage/forecast_bridge.hpp"
#include "cottage/building_identification.hpp"
#include "cottage/model_persistence.hpp"
#include "cottage/mpc_arbiter.hpp"
#include "cottage/mpc_planner.hpp"
#include "cottage/ruuvi.hpp"
#include "cottage/url_guard.hpp"
#include "cottage/mode_timing.hpp"
#include "cottage/price_bootstrap.hpp"
#include "cottage/plan_timeline.hpp"
#include "cottage_history.hpp"
#include "cottage/data_freshness.hpp"
#include "cottage/mitsubishi_climate_command.hpp"
#include "cottage/cn105_read_retry.hpp"
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
struct Cn105LinkIdentity {
    uint32_t endpoint=0, wifi=0;
    bool current() const {
        return endpoint==site_settings_endpoint_generation() &&
               wifi==cottage_wifi_link_generation();
    }
};
static Cn105LinkIdentity verified_cn105_link{}; // protected by lock_data
static Cn105LinkIdentity cn105_command_link{};
static MitsubishiCommandReadback cn105_command_readback{}; // protected by lock_data
// Legacy ESPHome /events exposes real device state as SSE, but the receipt
// does not prove a new UART poll. Keep the last observed values separately
// from the source-verified sample used by the optimizer/safety gate.
static CN105Telemetry sse_observed_pump{};
static ObservedModeDuration observed_mode_duration{};
// Protected by lock_data. Reject MPC results computed before an SSE mode change.
static uint32_t observed_mode_generation=0;
static uint64_t verified_cn105_request_since_ms=0;
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
static TaskHandle_t task_history=nullptr,task_midea=nullptr,task_local_lan=nullptr;
static bool shelly_relay_on=false;
// Production control is explicitly ARMED via local setting, never by OTA.
static std::atomic<bool> control_armed{false};
// Pump identity (CN105 URL/adapter) the current arming was given for.
static std::atomic<uint32_t> control_armed_endpoint{0};
static std::atomic<uint32_t> control_permission_generation{0};
static std::atomic<uint32_t> control_armed_midea_revision{0};
static std::atomic<uint32_t> control_armed_gree_revision{0};
static std::atomic<uint32_t> control_armed_daikin_revision{0},control_armed_daikin_wifi{0};
static std::atomic<uint32_t> control_armed_wf_rac_revision{0},control_armed_wf_rac_wifi{0};
static std::atomic<uint32_t> control_armed_cn105_direct_revision{0},control_armed_cn105_bus{0};
static std::atomic<uint32_t> control_armed_s21_revision{0},control_armed_s21_bus{0};
static std::atomic<uint32_t> control_armed_haier_revision{0},control_armed_haier_bus{0};
static std::atomic<uint32_t> control_armed_panasonic_revision{0},control_armed_panasonic_bus{0};
// Owner decision 18.11.2026: >= 30 min between HEAT<->OFF changes. Last change
// = last mode command sent, last change observed via bridge GET, boot or
// arming (unknown history -> wait). millis()-based, survives NTP jumps.
// Boot = unknown history (dwell starts at millis()==0). Shared by the web
// task (arming) and the optimizer task, serialized by mode_history_lock.
static MitsubishiModeHistory control_mode_history{};
static portMUX_TYPE mode_history_lock=portMUX_INITIALIZER_UNLOCKED;
static void control_note_observed_mode(bool heating) {
    const uint32_t now=millis();
    portENTER_CRITICAL(&mode_history_lock);
    control_mode_history.observe(heating,now);
    portEXIT_CRITICAL(&mode_history_lock);
}
static void control_note_mode_dispatch() {
    const uint32_t now=millis();
    portENTER_CRITICAL(&mode_history_lock);
    control_mode_history.command_dispatched(now);
    portEXIT_CRITICAL(&mode_history_lock);
}
static void control_reset_mode_history() {
    const uint32_t now=millis();
    portENTER_CRITICAL(&mode_history_lock);
    control_mode_history.reset(now);
    portEXIT_CRITICAL(&mode_history_lock);
}
static uint32_t control_ms_since_mode_change() {
    const uint32_t now=millis();
    portENTER_CRITICAL(&mode_history_lock);
    const uint32_t since=control_mode_history.since_change(now);
    portEXIT_CRITICAL(&mode_history_lock);
    return since;
}
static std::atomic<int> control_gate{static_cast<int>(ControlBlock::Disabled)};
static std::atomic<int> control_last_step{static_cast<int>(ControlStep::None)};
static std::atomic<uint32_t> control_commands_sent{0};
static std::atomic<uint32_t> control_commands_confirmed{0};
static std::atomic<uint32_t> control_commands_failed{0};
static std::atomic<int64_t> control_last_command_utc{0};
static std::atomic<int64_t> control_last_ack_utc{0};
static std::atomic<int64_t> control_last_bridge_read_utc{0};
// Observe the physical CN105 command path without serial access.
static std::atomic<int> control_post_stage{0};
static std::atomic<int> control_post_http_code{0};
static std::atomic<int> control_get_http_code{0};
// Track intermittent read-timeout recovery without logging secrets or IPs.
static std::atomic<uint32_t> cn105_get_retry_count{0};
static std::atomic<uint32_t> cn105_get_recovered_count{0};
static std::atomic<uint32_t> cn105_get_timeout_count{0};
// Distinguish a temporarily busy TLS/BLE network lease from a true shortage
// of internal RAM, without weakening the existing per-GET memory limits.
static std::atomic<uint32_t> cn105_get_lease_denials{0};
static std::atomic<uint32_t> cn105_get_lease_low_memory{0};
static std::atomic<uint32_t> cn105_get_lease_busy{0};
static std::atomic<uint32_t> cn105_get_last_free_internal{0};
static std::atomic<uint32_t> cn105_get_last_largest_internal{0};


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
// ---- Live MPC chain (identification -> receding-horizon plan -> arbiter) --
// The identifier and plan rows live in PSRAM and are written ONLY by the
// optimizer task; every field below is published/read under lock_data.
static BuildingIdentifier* identifier=nullptr;
static PlanRow* mpc_rows=nullptr;
static size_t mpc_row_count=0;
static TrendPoint* mpc_trend=nullptr;
static size_t mpc_trend_count=0;
static MpcPlanRecord mpc_plan{};
static CalibrationReport calibration_snapshot{};
static ArbiterOutput last_arbiter{};
static bool mpc_energy_in_kwh=false;
static double mpc_energy_kwh=0,mpc_cost=0,mpc_uncertainty_c=0;
static size_t horizon_price_slots=0,horizon_weather_slots=0,horizon_slots=0;
static HorizonLimit horizon_limit=HorizonLimit::None;
static uint32_t mpc_solve_ms=0,mpc_solve_worst_ms=0,mpc_solves=0,mpc_solver_failures=0;
static uint32_t identifier_nvs_saves=0,identifier_nvs_failures=0,identifier_nvs_skipped=0;
static bool identifier_restored=false;
// Owner-requested model reset: 0 none, 1 requested (not yet processed), 2 persisted,
// 3 the NVS erase/epoch write could not be verified (retried; never reported as done).
static std::atomic<int> identifier_reset_state{0};
static std::atomic<uint32_t> identifier_reset_failures{0};
// Slot (UTC start) whose MPC decision the pump bridge confirmed by readback.
// A plan decision is NEVER reported as executed before this confirmation.
static std::atomic<int64_t> mpc_confirmed_slot_utc{0};
static std::atomic<int> mpc_confirmed_heat{-1};
// Main has no owner confirmation of the Shelly measurement scope (the
// stacked settings PR adds SiteSettings::shelly_scope). Until the owner
// confirms "heat pump only", the meter is treated as whole-house/unknown:
// it is shown as measured consumption but never used for COP or the MPC's
// energy model.
static bool shelly_meter_pump_only() {return false;}
// Identity of the confirmed heat-pump-only meter (0 = none). When it changes
// or the confirmation is withdrawn the electric model restarts, so another
// meter's readings never blend into it. Constant on main (no confirmation).
static uint32_t shelly_meter_identity() {return 0;}
// The owner's HARD comfort limits (0 = unset) from the profile settings. The
// MPC band, plan and plan validity are bound to them; the price controller
// applies the same limits through apply_profile() / mode_comfort_limits_fit().
static ComfortLimits owner_comfort_limits() {
    const auto site=site_settings_snapshot();
    return ComfortLimits{site.comfort_min_c,site.comfort_max_c};
}

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
static bool parse_cn105_proof_event(const cJSON* root,int64_t now,
                                  const Cn105LinkIdentity& link) {
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
        const bool continuous=verified_cn105_link.current() && valid_cn105(pump,now);
        // A different setpoint in active HEAT changes the model's input.
        // Writing a setpoint while OFF is also the first stage of our own
        // OFF -> HEAT transition and leaves the current thermal state intact.
        const bool heating_target_changed=verified.mode==ObservedPumpMode::Heat &&
            std::fabs(verified.setpoint_c-pump.setpoint_c)>0.001;
        if(verified.mode!=pump.mode || !continuous || heating_target_changed) {
            verified_cn105_request_since_ms=static_cast<uint64_t>(esp_timer_get_time()/1000);
            ++observed_mode_generation;
            plan_count=0;
            last_plan.status=MpcStatus::InvalidInput;
        }
        observed_mode_duration.observe(verified.mode,
            static_cast<uint64_t>(esp_timer_get_time()/1000),continuous);
        pump=verified;
        verified_cn105_link=link;
        if(cn105_command_link.current() && cn105_command_link.endpoint==link.endpoint &&
           cn105_command_link.wifi==link.wifi)
            cn105_command_readback.observe(verified,now);
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
static void parse_sse_state(const char* raw,const Cn105LinkIdentity& link) {
    if(!link.current())return;
    cJSON* root=cJSON_ParseWithOpts(raw,nullptr,true);
    if(!root) return;
    const cJSON* key=cJSON_GetObjectItemCaseSensitive(root,"id");
    const int64_t now=clock_ok()?time(nullptr):0;
    if(!cJSON_IsString(key)||!now) {cJSON_Delete(root);return;}
    if(strcmp(key->valuestring,"text_sensor/Enevina CN105 Verified Snapshot")==0) {
        if(parse_cn105_proof_event(root,now,link))cn105_proof_envelope_active=true;
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
        // Browser-configured URL (Phase 2); compile-time value is only the default.
        const uint32_t settings_generation=site_settings_generation();
        Cn105LinkIdentity link{site_settings_endpoint_generation(),cottage_wifi_link_generation()};
        const auto settings=site_settings_snapshot();
        if(!link.current() || settings_generation!=site_settings_generation() ||
           cottage_ota_network_maintenance() || !wifi_ok.load() ||
           !clock_ok() ||
           std::strcmp(settings.pump_adapter,"mitsubishi_cn105_esphome")!=0 ||
           !valid_cn105_events_url(settings.cn105_url)) {
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
        const bool began=request.begin(client,settings.cn105_url);
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
                SseStateParser parser([](const char* json,void* context) {
                    parse_sse_state(json,*static_cast<const Cn105LinkIdentity*>(context));
                },&link);
                uint32_t last_activity_ms=millis();
                while(wifi_ok.load() && client.connected() &&
                      !cottage_ota_network_maintenance() &&
                      settings_generation==site_settings_generation() && link.current() &&
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
    for(;;) {
        if(cottage_ota_network_maintenance()) {
            vTaskDelay(pdMS_TO_TICKS(500));continue;
        }
        const auto settings=site_settings_snapshot();
        snprintf(url,sizeof(url),"%s/rpc/Switch.GetStatus?id=0",settings.shelly_url);
        if(wifi_ok.load() && clock_ok() &&
           valid_shelly_base_url(settings.shelly_url)) {
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
// ONE price rule for the MPC bridge, bootstrap, /api/status and /api/prices:
// browser-configured site tariff evaluated at the slot START in Finnish
// civil time (DST-safe, independent of the TZ variable).
static bool bridge_slot_price(void*,int64_t utc,double spot,double& total) {
    return site_slot_total_price(utc,spot,total);
}
// Model-free price-aware heating proposal, used only until calibrated MPC
// exists. A 2-hour window must contain 8 contiguous verified 15-minute
// TOTAL marginal-cost slots. This function never emits an actuator command.
static BootstrapDecision bootstrap_preview(int64_t now,
                                            const TemperatureSample& t) {
    BootstrapInput input{};
    input.indoor_fresh=valid_temperature(t,now);
    input.indoor_c=t.indoor_c;
    const CottageModeConfig mode=c23_mode_config();
    // Use the actual target from the same mode physics shown in /api/status.
    // Drying is outside-temperature+delta (can be BELOW pump's 16C setting),
    // so the controller uses HEAT 16C only when room temperature needs it.
    const CottageModePlan target_plan=c23_current_mode_plan();
    if(mode.mode==CottageMode::Cold) {
        BootstrapDecision off{};
        off.action=BootstrapAction::Off;
        off.reason=BootstrapReason::NoHeatingRequested;
        return off; // Explicit user-selected "Kylmillään".
    }
    if(!target_plan.has_target ||
       !std::isfinite(target_plan.target_c)) {
        return {}; // Cannot make up a drying temperature without outside data.
    }
    if(mode.mode==CottageMode::Drying && drying_blocks_heat(target_plan)) {
        // The existing humidity duration and hysteresis decide whether
        // drying heat is permitted at all. A target alone is NOT permission
        // to heat dry air, especially with prices unavailable. Once it is
        // permitted, the bootstrap's room deadband (HOLD) prevents toggling.
        BootstrapDecision no_heat{};
        no_heat.action=BootstrapAction::Off;
        no_heat.reason=BootstrapReason::NoHeatingRequested;
        no_heat.requested_room_c=target_plan.target_c;
        return no_heat;
    }
    input.desired_room_c=target_plan.target_c;
    const auto market=enevina_firmware::market_status();
    // The owner's profile and comfort limits come from the site settings.
    // Inconsistent values yield no decision, never a guessed one.
    const SiteSettings site=site_settings_snapshot();
    // Old/migrated NVS or a disrupted two-key transaction might carry a
    // claimed hard bound on the wrong side of the active mode target.
    // Refuse a physical proposal instead of silently dropping that bound.
    if(!mode_comfort_limits_fit(site,mode))return {};
    size_t window=8;
    if(!apply_profile(input,site.profile,site.comfort_min_c,site.comfort_max_c,
                      site.min_price_advantage_fraction,window))
        return {};
    constexpr size_t kMinSlots=8,kMaxSlots=16;
    std::array<enevina_firmware::PricePoint,kMaxSlots> points{};
    std::array<double,kMaxSlots> prices{};
    const int64_t start=(now/900)*900;
    const bool eligible=now>=1700000000 && market.clock_trusted &&
        market.current_price_valid && market.last_fetch_utc<=now &&
        market.last_fetch_utc>=now-36*3600 &&
        market.valid_until_utc>=start+static_cast<int64_t>(kMinSlots)*900;
    const size_t have=eligible ? enevina_firmware::future_prices(
        points.data(),points.size()) : 0;
    // Use as much of the profile's price window as is available and strictly
    // contiguous from the current slot, but never fewer than 8 slots.
    size_t n=0;
    while(n<window && n<have && n<kMaxSlots &&
          points[n].start_utc==start+static_cast<int64_t>(n)*900 &&
          site_slot_total_price(points[n].start_utc,points[n].spot_eur_per_kwh,prices[n]))
        ++n;
    const bool valid_prices=eligible && n>=kMinSlots;
    input.next_total_prices=prices.data();
    input.price_count=valid_prices?n:0;
    input.prices_contiguous_and_fresh=valid_prices;
    // Keep the profile-specific threshold already set by apply_profile().
    input.price_limit_scale=currency_limit_scale(market.currency);
    // The minimum *absolute* advantage is in the selected market currency;
    // keep approximately the same tariff-relative significance as EUR.
    input.min_price_advantage_eur_per_kwh=0.005*input.price_limit_scale;
    return bootstrap_price_decision(input);
}
// The room target and whether heat is permitted, from the SAME mode logic as
// bootstrap_preview(), independent of sensor freshness (the planner reports
// a stale sensor separately).
static void mpc_mode_target(bool& heating_allowed,double& target_c) {
    heating_allowed=false;target_c=0;
    const CottageModeConfig mode=c23_mode_config();
    if(mode.mode==CottageMode::Cold) return;
    const CottageModePlan plan=c23_current_mode_plan();
    if(!plan.has_target || !std::isfinite(plan.target_c)) return;
    if(mode.mode==CottageMode::Drying && drying_blocks_heat(plan)) return;
    heating_allowed=true;target_c=plan.target_c;
}
// Pump HEAT/OFF for the MPC chain (planning, learning, plan validity): ONLY a
// fresh, source-verified CN105 poll (pump-origin time + completed UART poll
// proof). An ESPHome GET/SSE receipt can repeat cached state while the pump
// link is down, so it is never used as evidence here (Codex P1, PR #63).
// `minutes` is the verified, uninterrupted time in that mode.
static enevina::PumpRoomState optimizer_pump_state(int64_t now,bool midea) {
    using namespace enevina;
    if(midea)return cottage_midea_room_state();
    if(cottage_gree_selected())return cottage_gree_room_state();
    if(cottage_daikin_selected())return cottage_daikin_room_state();
    if(cottage_daikin_dsiot_selected())return cottage_daikin_dsiot_room_state();
    if(cottage_wf_rac_selected())return cottage_wf_rac_room_state();
    if(cottage_cn105_direct_selected())return cottage_cn105_direct_room_state();
    if(cottage_daikin_s21_selected())return cottage_daikin_s21_room_state();
    if(cottage_haier_selected())return cottage_haier_room_state();
    if(cottage_panasonic_selected())return cottage_panasonic_room_state();
    PumpRoomState state{};
    xSemaphoreTake(lock_data,portMAX_DELAY);
    if(verified_cn105_link.current() && valid_cn105(pump,now) &&
       (pump.mode==ObservedPumpMode::Off || pump.mode==ObservedPumpMode::Heat)) {
        const auto ms=static_cast<uint64_t>(esp_timer_get_time()/1000);
        state.protocol=PumpRoomProtocol::Cn105EspHome;
        state.source=PumpStateSource::VerifiedBus;
        state.mode=pump.mode==ObservedPumpMode::Heat?PumpRoomMode::Heat:PumpRoomMode::Off;
        state.mode_minutes=observed_mode_duration.minutes(pump.mode,ms,true);
        state.request_minutes=static_cast<unsigned>(
            std::min<uint64_t>((ms-verified_cn105_request_since_ms)/60000,600));
        state.setpoint_c=pump.setpoint_c;
        state.request_generation=observed_mode_generation;
        state.endpoint_generation=verified_cn105_link.endpoint;
        state.link_generation=verified_cn105_link.wifi;
        state.heating=mitsubishi_heating_limits;
        state.evidence=pump.evidence;
    }
    xSemaphoreGive(lock_data);
    return state;
}
static bool direct_lan_pump_selected() {
    using namespace enevina;
    const auto* adapter=find_adapter(site_settings_snapshot().pump_adapter);
    return adapter && adapter->link==AdapterLink::VendorLanProtocol &&
        adapter_selectable_in_lite_firmware(adapter->id);
}
static bool uses_common_pump_state() {
    return direct_lan_pump_selected() || cottage_cn105_direct_selected() || cottage_daikin_s21_selected() || cottage_haier_selected() || cottage_panasonic_selected();
}
static bool known_heating_mode(const enevina::PumpRoomState& state) {
    return state.mode==enevina::PumpRoomMode::Off ||
        (state.mode==enevina::PumpRoomMode::Heat && std::isfinite(state.setpoint_c));
}
// ONE proposal for the physical control path: a calibrated, current MPC
// plan's first decision, otherwise the price controller, which itself falls
// back to a thermostat without prices. Never authorises a write by itself.
static ArbiterInput build_arbiter_input(int64_t now,const TemperatureSample& t) {
    ArbiterInput in{};
    in.now_utc=now;
    in.temperature_fresh=valid_temperature(t,now);
    in.indoor_c=t.indoor_c;
    in.bootstrap=bootstrap_preview(now,t);
    in.config_generation=c23_config_generation();
    in.settings_generation=site_settings_generation();
    in.limits=owner_comfort_limits();
    const bool midea=cottage_midea_selected();
    const auto state=optimizer_pump_state(now,midea);
    xSemaphoreTake(lock_data,portMAX_DELAY);
    in.calibration=calibration_snapshot.state;
    in.calibrated=calibration_snapshot.state==CalibrationState::Calibrated;
    in.plan=mpc_plan;
    xSemaphoreGive(lock_data);
    in.mode_generation=state.request_generation;
    in.pump_state_known=known_heating_mode(state);
    in.pump_heat=state.mode==enevina::PumpRoomMode::Heat;
    in.pump_setpoint_c=state.setpoint_c;
    in.own_transition_confirmed=in.plan.has_plan && (uses_common_pump_state()?
        state.automatic_queued_utc>=in.plan.computed_utc &&
        state.automatic_confirmed_utc>=state.automatic_queued_utc &&
        state.automatic_confirmed_utc<=now &&
        state.automatic_confirmed_utc<in.plan.first_utc+900:
        mpc_confirmed_slot_utc.load()==in.plan.first_utc &&
        mpc_confirmed_heat.load()==(in.plan.first_heat?1:0));
    return in;
}
static bool mpc_state_read_back(const ArbiterInput& in) {
    return in.own_transition_confirmed && in.pump_state_known &&
        in.pump_heat==in.plan.first_heat &&
        (!in.plan.first_heat ||
         std::fabs(in.pump_setpoint_c-in.plan.first_setpoint_c)<=0.001);
}
static BootstrapDecision active_control_proposal(int64_t now,const TemperatureSample& t,
                                                 ArbiterOutput* detail=nullptr) {
    const ArbiterOutput out=arbitrate(build_arbiter_input(now,t));
    if(detail) *detail=out;
    return out.proposal;
}
// CN105 is a separate local ESPHome gateway. These HTTP calls are the only
// physical write path and are permitted solely by an explicitly armed
// pilot plus fresh independent sensor, prices and gateway telemetry.
static String cn105_local_url(const char* suffix) {
    const auto settings=site_settings_snapshot();
    if(!suffix || !valid_cn105_events_url(settings.cn105_url))return {};
    String root(settings.cn105_url);
    root.remove(root.length()-strlen("/events"));
    root+=suffix;
    return root;
}
static bool cn105_selected() {
    const auto settings=site_settings_snapshot();
    return enevina::adapter_allowed_cn105_physical_writes(settings.pump_adapter);
}
static TemperatureSample selected_room_sample(int64_t now,const RoomTemperatureConfig& config) {
    using namespace enevina;
    if(config.source==RoomTemperatureSource::Ruuvi)
        return room_temperature_sample(config,c23_temperature_sample(),{},now);
    if(!wifi_ok.load() || !clock_ok())return {};
    PumpRoomState state{};
    if(cottage_midea_selected()) {
        state=cottage_midea_room_state();
    } else if(cottage_gree_selected()) {
        state=cottage_gree_room_state();
    } else if(cottage_daikin_selected()) {
        state=cottage_daikin_room_state();
    } else if(cottage_daikin_dsiot_selected()) {
        state=cottage_daikin_dsiot_room_state();
    } else if(cottage_wf_rac_selected()) {
        state=cottage_wf_rac_room_state();
    } else if(cottage_cn105_direct_selected()) {
        state=cottage_cn105_direct_room_state();
    } else if(cottage_daikin_s21_selected()) {
        state=cottage_daikin_s21_room_state();
    } else if(cottage_haier_selected()) {
        state=cottage_haier_room_state();
    } else if(cottage_panasonic_selected()) {
        state=cottage_panasonic_room_state();
    } else if(cn105_selected()) {
        xSemaphoreTake(lock_data,portMAX_DELAY);
        if(verified_cn105_link.current() && valid_cn105(pump,now)) {
            state.source=PumpStateSource::VerifiedBus;
            state.indoor_c=pump.indoor_c;
            state.evidence=pump.evidence;
        }
        xSemaphoreGive(lock_data);
    }
    return room_temperature_sample(config,{},state,now);
}
static TemperatureSample selected_room_sample(int64_t now) {
    return selected_room_sample(now,site_room_temperature_config());
}
static bool cn105_bridge_get(bool& heating,double& target) {
    if(!cn105_selected()) return false;
    const String url=cn105_local_url(
        "/climate/Mitsubishi%20Electric%20MSZ-FD25VA?detail=all");
    if(url.isEmpty() || !wifi_ok.load())return false;
    // ESP32-S3 shares 2.4-GHz radio/internal RAM with BLE and HTTPS. The old
    // 1.8-second HTTP response deadline intermittently returned -11 even
    // though the CN105 ESPHome gateway stayed healthy. Retry only READS
    // after transport timeouts, never physical POST commands.
    for(unsigned attempt=0;attempt<cn105_read_max_attempts;++attempt) {
        int code=0;
        bool verified=false;
        {
            // Reacquire the memory/network lease for each attempt. Never hold
            // its shared mutex while waiting between retries.
            // BLE scans may hold the shared network mutex for up to 4 s.
            // A 1 s GET wait repeatedly collided with them and made an
            // otherwise healthy CN105 bridge appear INPUTS_UNVERIFIED.
            // Keep the same 72-KiB/16-KiB memory floors; only wait longer.
            enevina_firmware::NetworkBudgetLease lease(5000,72*1024,16*1024);
            if(!lease.held()) {
                control_get_http_code.store(0);
                cn105_get_lease_denials.fetch_add(1);
                const auto& detail=lease.diagnostic();
                cn105_get_last_free_internal.store(detail.free_internal);
                cn105_get_last_largest_internal.store(detail.largest_internal);
                if(detail.failure==enevina_firmware::NetworkBudgetFailure::LowMemory)
                    cn105_get_lease_low_memory.fetch_add(1);
                else if(detail.failure==enevina_firmware::NetworkBudgetFailure::MutexBusy)
                    cn105_get_lease_busy.fetch_add(1);
                // Retry only the *read* after a transient mutex/heap failure.
                // The physical POST path never retries automatically.
                if(attempt+1<cn105_read_max_attempts) {
                    cn105_get_retry_count.fetch_add(1);
                    vTaskDelay(pdMS_TO_TICKS(cn105_read_retry_delay_ms));
                    continue;
                }
                return false;
            }
            WiFiClient client;
            HTTPClient req;
            req.setConnectTimeout(cn105_read_connect_timeout_ms);
            req.setTimeout(cn105_read_response_timeout_ms);
            req.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            if(!req.begin(client,url)) {
                control_get_http_code.store(0);
                return false;
            }
            req.addHeader("Cache-Control","no-cache");
            code=req.GET();
            control_get_http_code.store(code);
            if(code==cn105_http_read_timeout)
                cn105_get_timeout_count.fetch_add(1);
            const int body_size=req.getSize();
            if(code==200 && body_size>0 && body_size<1800) {
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
                            verified=true;
                        }
                        cJSON_Delete(root);
                    }
                }
            }
            req.end();
        }
        if(verified) {
            if(attempt>0)cn105_get_recovered_count.fetch_add(1);
            control_last_bridge_read_utc.store(time(nullptr));
            return true;
        }
        // Reject malformed/unauthorized/non-200 responses; only -11 (HTTP
        // read timeout) and -5 (connection lost) are transient retry cases.
        if(!cn105_should_retry_read(code,attempt) || !wifi_ok.load())
            return false;
        cn105_get_retry_count.fetch_add(1);
        vTaskDelay(pdMS_TO_TICKS(cn105_read_retry_delay_ms));
    }
    return false;
}
static enevina::PumpRoomState cn105_room_state() {
    using namespace enevina;
    PumpRoomState state{};
    const auto endpoint=site_settings_endpoint_generation();
    const auto link=cottage_wifi_link_generation();
    bool heat=false;
    double setpoint=0;
    if(!cn105_bridge_get(heat,setpoint) || endpoint!=site_settings_endpoint_generation() ||
       link!=cottage_wifi_link_generation())return state;
    state.type=HeatPumpType::AirToAir;
    state.protocol=PumpRoomProtocol::Cn105EspHome;
    state.endpoint_generation=endpoint;
    state.link_generation=link;
    state.mode=heat?PumpRoomMode::Heat:PumpRoomMode::Off;
    state.setpoint_c=setpoint;
    state.heating=mitsubishi_heating_limits;
    state.source=PumpStateSource::Gateway;
    state.evidence.received_utc=control_last_bridge_read_utc.load();
    return state;
}

void cottage_midea_automatic_cancel() {
    control_armed.store(false);
    control_permission_generation.fetch_add(1);
}
bool cottage_midea_automatic_enabled() {
    return control_armed.load() && cottage_midea_selected() &&
        control_armed_endpoint.load()==site_settings_endpoint_generation() &&
        cottage_midea_configuration_current(control_armed_midea_revision.load());
}
bool cottage_midea_automatic_write_current(uint32_t authorization_generation,
        uint32_t config_generation,uint32_t site_generation) {
    if(control_permission_generation.load()!=authorization_generation ||
       !cottage_midea_automatic_enabled() || !wifi_ok.load() || !clock_ok() ||
       cottage_ota_network_maintenance() || !c23_config_storage_verified() ||
       c23_config_generation()!=config_generation ||
       site_settings_generation()!=site_generation)
        return false;
    return c23_mode_config().mode==CottageMode::Cold ||
           selected_room_sample(time(nullptr)).received;
}

void cottage_gree_automatic_cancel() {
    cottage_midea_automatic_cancel();
}
bool cottage_gree_automatic_enabled() {
    return control_armed.load() && cottage_gree_selected() &&
        control_armed_endpoint.load()==site_settings_endpoint_generation() &&
        control_armed_gree_revision.load()==cottage_gree_configuration_generation();
}
void cottage_daikin_automatic_cancel() {
    if(cottage_daikin_selected())cottage_midea_automatic_cancel();
}
bool cottage_daikin_automatic_enabled() {
    return control_armed.load() && cottage_daikin_selected() &&
        control_armed_endpoint.load()==site_settings_endpoint_generation() &&
        control_armed_daikin_wifi.load()==cottage_wifi_link_generation() &&
        control_armed_daikin_revision.load()==cottage_daikin_configuration_generation();
}
void cottage_wf_rac_automatic_cancel() {
    if(cottage_wf_rac_selected())cottage_midea_automatic_cancel();
}
bool cottage_wf_rac_automatic_enabled() {
    return control_armed.load() && cottage_wf_rac_selected() &&
        control_armed_endpoint.load()==site_settings_endpoint_generation() &&
        control_armed_wf_rac_wifi.load()==cottage_wifi_link_generation() &&
        control_armed_wf_rac_revision.load()==cottage_wf_rac_configuration_generation();
}
void cottage_cn105_direct_automatic_cancel() {
    if(cottage_cn105_direct_selected())cottage_midea_automatic_cancel();
}
bool cottage_cn105_direct_automatic_enabled() {
    return control_armed.load() && cottage_cn105_direct_selected() &&
        control_armed_endpoint.load()==site_settings_endpoint_generation() &&
        control_armed_cn105_bus.load()==cottage_cn105_direct_bus_generation() &&
        control_armed_cn105_direct_revision.load()==cottage_cn105_direct_configuration_generation();
}
void cottage_daikin_s21_automatic_cancel() {
    if(cottage_daikin_s21_selected())cottage_midea_automatic_cancel();
}
void cottage_haier_automatic_cancel() {
    if(cottage_haier_selected())cottage_midea_automatic_cancel();
}
void cottage_panasonic_automatic_cancel() {
    if(cottage_panasonic_selected())cottage_midea_automatic_cancel();
}
bool cottage_daikin_s21_automatic_enabled() {
    return control_armed.load() && cottage_daikin_s21_selected() &&
        control_armed_endpoint.load()==site_settings_endpoint_generation() &&
        control_armed_s21_bus.load()==cottage_daikin_s21_bus_generation() &&
        control_armed_s21_revision.load()==cottage_daikin_s21_configuration_generation();
}
bool cottage_haier_automatic_enabled() {
    return control_armed.load() && cottage_haier_selected() &&
        control_armed_endpoint.load()==site_settings_endpoint_generation() &&
        control_armed_haier_bus.load()==cottage_haier_bus_generation() &&
        control_armed_haier_revision.load()==cottage_haier_configuration_generation();
}
bool cottage_panasonic_automatic_enabled() {
    return control_armed.load() && cottage_panasonic_selected() &&
        control_armed_endpoint.load()==site_settings_endpoint_generation() &&
        control_armed_panasonic_bus.load()==cottage_panasonic_bus_generation() &&
        control_armed_panasonic_revision.load()==cottage_panasonic_configuration_generation();
}
static bool active_control_armed(bool midea) {
    if(midea)return cottage_midea_automatic_enabled();
    if(cottage_gree_selected())return cottage_gree_automatic_enabled();
    if(cottage_daikin_selected())return cottage_daikin_automatic_enabled();
    if(cottage_wf_rac_selected())return cottage_wf_rac_automatic_enabled();
    if(cottage_cn105_direct_selected())return cottage_cn105_direct_automatic_enabled();
    if(cottage_daikin_s21_selected())return cottage_daikin_s21_automatic_enabled();
    if(cottage_haier_selected())return cottage_haier_automatic_enabled();
    if(cottage_panasonic_selected())return cottage_panasonic_automatic_enabled();
    return cn105_selected() && control_armed.load();
}
static uint32_t pump_configuration_revision(bool midea) {
    if(midea)return cottage_midea_configuration_generation();
    if(cottage_gree_selected())return cottage_gree_configuration_generation();
    if(cottage_daikin_selected())return cottage_daikin_configuration_generation();
    if(cottage_daikin_dsiot_selected())return cottage_daikin_dsiot_configuration_generation();
    if(cottage_cn105_direct_selected())return cottage_cn105_direct_configuration_generation();
    if(cottage_daikin_s21_selected())return cottage_daikin_s21_configuration_generation();
    if(cottage_haier_selected())return cottage_haier_configuration_generation();
    if(cottage_panasonic_selected())return cottage_panasonic_configuration_generation();
    return cottage_wf_rac_selected()?cottage_wf_rac_configuration_generation():0;
}
static bool expected_control_proposal_current(BootstrapAction expected,double target_c) {
    const int64_t now=time(nullptr);
    const auto current=active_control_proposal(now,selected_room_sample(now));
    return current.action==expected && (expected==BootstrapAction::Off ||
        (expected==BootstrapAction::HeatSetpoint &&
         std::fabs(current.heat_setpoint_c-target_c)<=0.001));
}

bool cottage_midea_automatic_proposal_current(BootstrapAction expected,double target_c) {
    return expected_control_proposal_current(expected,target_c);
}
static bool queued_proposal_current(uint32_t authorization,uint32_t config,
        uint32_t settings,const BootstrapDecision& proposal,bool enabled) {
    if(control_permission_generation.load()!=authorization || !enabled ||
       !wifi_ok.load() || !clock_ok() ||
       cottage_ota_network_maintenance() || !c23_config_storage_verified() ||
       c23_config_generation()!=config || site_settings_generation()!=settings)
        return false;
    const auto now=time(nullptr);
    if(c23_mode_config().mode!=CottageMode::Cold && !selected_room_sample(now).received)
        return false;
    return expected_control_proposal_current(proposal.action,proposal.heat_setpoint_c);
}

bool cottage_gree_automatic_write_current(uint32_t authorization,uint32_t config,
        uint32_t settings,const BootstrapDecision& proposal) {
    return queued_proposal_current(authorization,config,settings,proposal,
                                       cottage_gree_automatic_enabled());
}
bool cottage_daikin_automatic_write_current(uint32_t authorization,uint32_t config,
        uint32_t settings,const BootstrapDecision& proposal) {
    return queued_proposal_current(authorization,config,settings,proposal,
                                       cottage_daikin_automatic_enabled());
}

bool cottage_wf_rac_automatic_write_current(uint32_t authorization,uint32_t config,
        uint32_t settings,const BootstrapDecision& proposal) {
    return queued_proposal_current(authorization,config,settings,proposal,
                                       cottage_wf_rac_automatic_enabled());
}

bool cottage_cn105_direct_automatic_write_current(uint32_t authorization,uint32_t config,
        uint32_t settings,const BootstrapDecision& proposal) {
    return queued_proposal_current(authorization,config,settings,proposal,
                                       cottage_cn105_direct_automatic_enabled());
}

bool cottage_daikin_s21_automatic_write_current(uint32_t authorization,uint32_t config,
        uint32_t settings,const BootstrapDecision& proposal) {
    return queued_proposal_current(authorization,config,settings,proposal,cottage_daikin_s21_automatic_enabled());
}
bool cottage_haier_automatic_write_current(uint32_t authorization,uint32_t config,
        uint32_t settings,const BootstrapDecision& proposal) {
    return queued_proposal_current(authorization,config,settings,proposal,cottage_haier_automatic_enabled());
}
bool cottage_panasonic_automatic_write_current(uint32_t authorization,uint32_t config,
        uint32_t settings,const BootstrapDecision& proposal) {
    return queued_proposal_current(authorization,config,settings,proposal,cottage_panasonic_automatic_enabled());
}

static bool cn105_bridge_post(const MitsubishiHttpRequest& command,
                              uint32_t config_generation,uint32_t site_generation,
                              uint32_t link_generation,ControlStep step,double required_target,
                              bool expected_heat,double expected_setpoint) {
    if(!cn105_selected())return false;
    control_post_stage.store(1);
    control_post_http_code.store(0);
    // A browser settings change (URL, tariff) supersedes an in-flight command
    // exactly like a mode change does; no new gate, same cancellation points.
    if(!command.valid || !control_armed.load() || !wifi_ok.load() ||
       control_armed_endpoint.load()!=site_settings_endpoint_generation()) {
        control_post_stage.store(-1);return false;
    }
    const String url=cn105_local_url(command.path.data());
    if(url.isEmpty()) {control_post_stage.store(-2);return false;}
    // Never hold NetworkBudgetLease across cn105_bridge_get(): it owns its
    // own non-recursive network mutex. Revalidate after this blocking GET.
    const int64_t now=time(nullptr);
    const auto current_temp=selected_room_sample(now);
    // A direct CN105 HTTP GET is the live device check. An unrelated SSE
    // heartbeat or a proprietary "Verified Snapshot" is not required.
    const CottageMode current_mode=c23_mode_config().mode;
    if(current_mode!=CottageMode::Cold &&
       !valid_temperature(current_temp,now))return false;
    bool bridge_heat=false;double bridge_setpoint=0;
    if(!cn105_bridge_get(bridge_heat,bridge_setpoint)) {
        control_post_stage.store(-3);return false;
    }
    control_post_stage.store(3);
    if(bridge_heat!=expected_heat || std::fabs(bridge_setpoint-expected_setpoint)>0.001) {
        control_note_observed_mode(bridge_heat);
        return false;
    }
    const auto latest=active_control_proposal(now,current_temp);
    if((step==ControlStep::Setpoint || step==ControlStep::SetHeat) &&
       (latest.action!=BootstrapAction::HeatSetpoint ||
        std::fabs(latest.heat_setpoint_c-required_target)>0.001))return false;
    if(step==ControlStep::SetOff && latest.action!=BootstrapAction::Off)
        return false;
    enevina_firmware::NetworkBudgetLease lease(1000,72*1024,16*1024);
    if(!lease.held()) {control_post_stage.store(-4);return false;}
    control_post_stage.store(4);
    // Network-budget wait is another cancellation point. A user's updated
    // settings always supersede an in-flight automatic command.
    if(!control_armed.load() || !c23_config_storage_verified() ||
       c23_config_generation()!=config_generation ||
       site_settings_generation()!=site_generation ||
       !wifi_ok.load() || !clock_ok())return false;
    const int64_t refreshed_utc=time(nullptr);
    const auto refreshed=active_control_proposal(refreshed_utc,selected_room_sample(refreshed_utc));
    if((step==ControlStep::Setpoint || step==ControlStep::SetHeat) &&
       (refreshed.action!=BootstrapAction::HeatSetpoint ||
        std::fabs(refreshed.heat_setpoint_c-required_target)>0.001))
        return false;
    if(step==ControlStep::SetOff &&
       refreshed.action!=BootstrapAction::Off)return false;
    WiFiClient client;
    HTTPClient req;
    req.setConnectTimeout(1500);req.setTimeout(1800);
    req.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!req.begin(client,url)) {control_post_stage.store(-5);return false;}
    req.addHeader("Content-Type","application/x-www-form-urlencoded");
    // Arduino-ESP32 HTTPClient::sendRequest only adds Content-Length when
    // payload size > 0. ESPHome's local WebServer rejects POST without it
    // with HTTP 411. Explicitly send zero length for command-only queries.
    req.addHeader("Content-Length","0");
    control_post_stage.store(5);
    // Last cancellation point after all potentially blocking preparation.
    xSemaphoreTake(lock_data,portMAX_DELAY);
    const bool dispatch=control_armed.load() && c23_config_storage_verified() &&
        c23_config_generation()==config_generation &&
        site_settings_generation()==site_generation &&
        cn105_command_link.current() && cn105_command_link.wifi==link_generation &&
        control_armed_endpoint.load()==cn105_command_link.endpoint;
    if(dispatch)cn105_command_readback.sent={step,required_target,time(nullptr)};
    xSemaphoreGive(lock_data);
    if(!dispatch) {
        req.end();
        return false;
    }
    // Codex P2 (post-merge #28): start the dwell only when a mode POST is
    // really dispatched (after every precondition); a failed preparation
    // must not block the next attempt for 30 min. An uncertain response or
    // failed readback still counts: the command may have taken effect.
    if(step==ControlStep::SetHeat || step==ControlStep::SetOff)
        control_note_mode_dispatch();
    const int code=req.POST("");
    control_post_http_code.store(code);
    control_post_stage.store(code>=200 && code<300?6:-6);
    req.end();
    return code>=200 && code<300;
}
// Called only in the existing optimizer task, no additional RTOS stack,
// watchdog or scheduled foreground WebServer blocking.
static bool control_current_image_sha256(uint8_t out[32]) {
    const esp_partition_t* running=esp_ota_get_running_partition();
    return running && esp_partition_get_sha256(running,out)==ESP_OK;
}
// Persistent pump authorization belongs to one image and pump identity.
// The installed 20-KiB NVS partition can fill with disposable weather/market
// caches.  Only an explicitly authorized /api/control/arm request may
// recover space, and ONLY by erasing the two known re-fetchable cache keys.
// Never erase Wi-Fi, the signed OTA journal or any previous control intent.
static std::atomic<uint32_t> control_arm_nvs_reclaimed{0};
static std::atomic<uint32_t> control_arm_nvs_save_failures{0};
static std::atomic<int32_t> control_arm_nvs_last_error{0};
static bool control_erase_disposable_cache(const char* nspace,const char* key) {
    nvs_handle_t handle=0;
    const esp_err_t opened=nvs_open(nspace,NVS_READWRITE,&handle);
    if(opened!=ESP_OK) {
        control_arm_nvs_last_error.store(opened);
        return false;
    }
    const esp_err_t erased=nvs_erase_key(handle,key);
    const esp_err_t committed=erased==ESP_OK?nvs_commit(handle):erased;
    nvs_close(handle);
    control_arm_nvs_last_error.store(committed);
    if(committed==ESP_OK) {
        control_arm_nvs_reclaimed.fetch_add(1);
        ESP_LOGW("enevina_ctl","Reclaimed disposable NVS cache %s/%s",nspace,key);
        return true;
    }
    return false;
}
static bool control_save_user_arm(const StoredControlIntent& record) {
    // Unlike Preferences::putBytes, the native API exposes the precise NVS
    // open/set/commit error. Never replace a failure with cache-erase status.
    esp_err_t last_write_error=ESP_FAIL;
    const auto store=[&]() {
        nvs_handle_t handle=0;
        esp_err_t err=nvs_open("cottage_ctl",NVS_READWRITE,&handle);
        if(err==ESP_OK) {
            err=nvs_set_blob(handle,"intent1",&record,sizeof(record));
            if(err==ESP_OK)err=nvs_commit(handle);
            nvs_close(handle);
        }
        last_write_error=err;
        return err==ESP_OK;
    };
    if(enevina::cottage::persist_with_optional_cache_reclaim(
            store,control_erase_disposable_cache)) {
        control_arm_nvs_last_error.store(ESP_OK);
        return true;
    }
    control_arm_nvs_last_error.store(last_write_error);
    control_arm_nvs_save_failures.fetch_add(1);
    return false;
}
// Also called from the site-settings HTTP handler BEFORE it commits a changed
// endpoint. The current physical command is stopped first, then the previous
// device-specific NVS authorization is durably revoked.
bool cottage_control_revoke_saved_intent() {
    control_armed.store(false);
    control_permission_generation.fetch_add(1);
    uint8_t digest[32]{};
    (void)control_current_image_sha256(digest); // disabled intent needs no hash
    const auto record=control_intent_for(
        site_settings_snapshot(),false,digest);
    Preferences prefs;
    if(!prefs.begin("cottage_ctl",false))return false;
    const bool stored=
        prefs.putBytes("intent1",&record,sizeof(record))==sizeof(record);
    prefs.end();
    return stored;
}
static void cottage_control_disarm() {
    if(!cottage_control_revoke_saved_intent())
        Serial.println("CONTROL_WARN: durable disarm write failed");
}
static void cottage_control_tick() {
    control_gate.store(static_cast<int>(ControlBlock::Disabled));
    const bool midea=cottage_midea_selected();
    const bool gree=cottage_gree_selected();
    const bool daikin=cottage_daikin_selected();
    const bool wf_rac=cottage_wf_rac_selected();
    const bool direct_cn105=cottage_cn105_direct_selected();
    const bool s21=cottage_daikin_s21_selected();
    const bool haier=cottage_haier_selected();
    const bool panasonic=cottage_panasonic_selected();
    if(!midea && !gree && !daikin && !wf_rac && !direct_cn105 && !s21 && !haier && !panasonic && !cn105_selected()) {
        if(control_armed.load())cottage_control_disarm();
        return;
    }
    if(!control_armed.load())return;
    const uint32_t authorization_generation=control_permission_generation.load();
    const uint32_t site_generation=site_settings_generation();
    if((midea || gree || daikin || wf_rac || direct_cn105 || s21 || haier || panasonic) && !active_control_armed(midea))return;
    // Arming is bound to the configured pump. A changed CN105 endpoint or
    // adapter revokes it; a reboot restores only the same pump identity.
    if(control_armed_endpoint.load()!=site_settings_endpoint_generation()) {
        cottage_control_disarm();
        return;
    }
    if(!wifi_ok.load() || !clock_ok() || !c23_config_storage_verified()) {
        control_gate.store(static_cast<int>(ControlBlock::InputsUnverified));
        return;
    }
    const CottageModeConfig mode=c23_mode_config();
    const uint32_t config_generation=c23_config_generation();
    const int64_t now=time(nullptr);
    const auto sample=selected_room_sample(now);
    ArbiterOutput arbitration{};
    const auto proposal=active_control_proposal(now,sample,&arbitration);
    xSemaphoreTake(lock_data,portMAX_DELAY);
    last_arbiter=arbitration;
    xSemaphoreGive(lock_data);
    // CN105 direct GET is authoritative enough for this local pilot;
    // optional SSE callbacks do not block physical control.
    if((mode.mode!=CottageMode::Cold &&
        !valid_temperature(sample,now)) ||
       proposal.action==BootstrapAction::Unavailable) {
        control_gate.store(static_cast<int>(ControlBlock::InputsUnverified));
        return;
    }
    ControlSnapshot candidate{};
    candidate.armed=control_armed.load();
    candidate.heating_mode=(mode.mode==CottageMode::Cold ||
          mode.mode==CottageMode::Drying ||
          mode.mode==CottageMode::Hold ||
          mode.mode==CottageMode::Occupied);
    candidate.temperature_fresh=mode.mode==CottageMode::Cold ||
                                valid_temperature(sample,now);
    candidate.market_fresh=proposal.action!=BootstrapAction::Unavailable;
    candidate.proposal=proposal;
    if(midea) {
        // Midea's direct LAN worker uses the same decision gate. The existing
        // calibrated CN105 model cannot transfer to a different pump.
        const auto reading=cottage_midea_room_state();
        const auto gate=cottage_midea_submit_automatic(reading,candidate,
            authorization_generation,config_generation,site_generation);
        control_gate.store(static_cast<int>(gate));
        return;
    }
    if(gree) {
        const auto reading=cottage_gree_room_state();
        const auto gate=cottage_gree_submit_automatic(reading,candidate,
            authorization_generation,config_generation,site_generation);
        control_gate.store(static_cast<int>(gate));
        return;
    }
    if(daikin) {
        const auto reading=cottage_daikin_room_state();
        const auto gate=cottage_daikin_submit_automatic(reading,candidate,
            authorization_generation,config_generation,site_generation);
        control_gate.store(static_cast<int>(gate));
        return;
    }
    if(wf_rac) {
        const auto current=cottage_wf_rac_room_state();
        const auto gate=cottage_wf_rac_submit_automatic(current,candidate,
            authorization_generation,config_generation,site_generation);
        control_gate.store(static_cast<int>(gate));
        return;
    }
    if(direct_cn105) {
        const auto current=cottage_cn105_direct_room_state();
        const auto gate=cottage_cn105_direct_submit_automatic(current,candidate,
            authorization_generation,config_generation,site_generation);
        control_gate.store(static_cast<int>(gate));
        return;
    }
    if(s21) {
        const auto current=cottage_daikin_s21_room_state();
        const auto gate=cottage_daikin_s21_submit_automatic(current,candidate,
            authorization_generation,config_generation,site_generation);
        control_gate.store(static_cast<int>(gate));
        return;
    }
    if(haier) {
        const auto current=cottage_haier_room_state();
        const auto gate=cottage_haier_submit_automatic(current,candidate,
            authorization_generation,config_generation,site_generation);
        control_gate.store(static_cast<int>(gate));
        return;
    }
    if(panasonic) {
        const auto current=cottage_panasonic_room_state();
        const auto gate=cottage_panasonic_submit_automatic(current,candidate,
            authorization_generation,config_generation,site_generation);
        control_gate.store(static_cast<int>(gate));
        return;
    }
    // A HOLD proposal does not authorize any physical command. A failed
    // *optional* bridge observation must not mislabel it as an unsafe    // heating command; still attempt a read to maintain the 30-min dwell
    // history and avoid a stale observation gap at the next HEAT request.
    if(proposal.action==BootstrapAction::Hold) {
        bool observed_heat=false;double observed_setpoint=0;
        if(cn105_bridge_get(observed_heat,observed_setpoint))
            control_note_observed_mode(observed_heat);
        control_gate.store(static_cast<int>(ControlBlock::NoProposal));
        return;
    }
    const auto current=cn105_room_state();
    if(current.source!=enevina::PumpStateSource::Gateway) {
        control_gate.store(static_cast<int>(ControlBlock::InputsUnverified));
        return;
    }
    // Preflight GET can block: old sensor, mode and user authorization
    // snapshots must never be carried into the physical POST.
    if(!control_armed.load() ||
       config_generation!=c23_config_generation() ||
       site_generation!=site_settings_generation() ||
       (mode.mode!=CottageMode::Cold && !selected_room_sample(time(nullptr)).received)) {
        control_gate.store(static_cast<int>(ControlBlock::InputsUnverified));
        return;
    }
    candidate.cn105_link_fresh=true; // direct bridge GET above succeeded
    candidate.bridge_get_ok=true; // separate live ESPHome GET succeeded
    const bool current_heat=current.mode==enevina::PumpRoomMode::Heat;
    candidate.current_heat=current_heat;
    candidate.current_setpoint_c=current.setpoint_c;
    control_note_observed_mode(current_heat);
    candidate.ms_since_mode_change=control_ms_since_mode_change();
    const auto decision=choose_mitsubishi_control(candidate);
    control_gate.store(static_cast<int>(decision.gate));
    // The pump bridge already reports the MPC decision's state: that GET is
    // the readback for this slot's decision.
    if(decision.gate==ControlBlock::AlreadyApplied &&
       arbitration.source==ProposalSource::Mpc) {
        mpc_confirmed_slot_utc.store((now/900)*900);
        mpc_confirmed_heat.store(proposal.action==BootstrapAction::HeatSetpoint?1:0);
    }
    if(decision.step==ControlStep::None)return;
    MitsubishiHttpRequest request{};
    if(decision.step==ControlStep::Setpoint)
        request=mitsubishi_setpoint_request(decision.target_c);
    else request=mitsubishi_mode_request(
        decision.step==ControlStep::SetHeat?MitsubishiMode::Heat:
                                            MitsubishiMode::Off);
    if(!request.valid || !control_armed.load())return;
    xSemaphoreTake(lock_data,portMAX_DELAY);
    if(!cn105_command_link.current())cn105_command_readback={};
    cn105_command_link={current.endpoint_generation,current.link_generation};
    cn105_command_readback.requested={decision.step,decision.target_c,now};
    xSemaphoreGive(lock_data);
    control_last_step.store(static_cast<int>(decision.step));
    control_last_command_utc.store(now);
    control_commands_sent.fetch_add(1);
    const uint32_t link_generation=current.link_generation;
    const bool sent=link_generation==cottage_wifi_link_generation() &&
        cn105_bridge_post(request,config_generation,site_generation,link_generation,decision.step,
                         decision.target_c,current_heat,current.setpoint_c);
    // ESPHome may acknowledge the POST before updating its REST state.
    // Confirm only what the bridge GET reports, never claim UART proof.
    bool confirmed=false;
    bool last_heating=false;
    double last_setpoint=0;
    if(sent) {
        for(unsigned attempt=0;attempt<5 && control_armed.load();++attempt) {
            vTaskDelay(pdMS_TO_TICKS(600));
            bool read_heating=false;
            double read_setpoint=0;
            if(!cn105_bridge_get(read_heating,read_setpoint))continue;
            if(site_settings_generation()!=site_generation ||
               cottage_wifi_link_generation()!=link_generation)break;
            control_note_observed_mode(read_heating);
            last_heating=read_heating;last_setpoint=read_setpoint;
            confirmed=decision.step==ControlStep::Setpoint ?
                std::fabs(read_setpoint-decision.target_c)<=0.001 :
                decision.step==ControlStep::SetHeat ? read_heating :
                !read_heating;
            if(confirmed)break;
        }
    }
    if(confirmed) {
        control_post_stage.store(8);
        control_commands_confirmed.fetch_add(1);
        control_last_ack_utc.store(time(nullptr));
        // An MPC decision counts as executed only when the readback shows
        // the planned state itself (the requested HEAT setpoint or OFF), not merely an
        // intermediate setpoint step.
        const bool planned_heat=proposal.action==BootstrapAction::HeatSetpoint;
        if(arbitration.source==ProposalSource::Mpc &&
           (planned_heat?(last_heating && std::fabs(last_setpoint-proposal.heat_setpoint_c)<0.01):!last_heating)) {
            mpc_confirmed_slot_utc.store((now/900)*900);
            mpc_confirmed_heat.store(planned_heat?1:0);
        }
    } else {
        if(control_post_stage.load()==6)control_post_stage.store(-8);
        control_commands_failed.fetch_add(1);
    }
}
static String control_connection_key(bool midea) {
    return String(site_settings_endpoint_generation())+"/"+
        String(cottage_wifi_link_generation())+"/"+
        String(pump_configuration_revision(midea));
}
static void cottage_control_registration() {
    // User's previously chosen automation survives a same-image restart.
    // A new OTA image (including pending validation) does NOT inherit it.
    // The pump identity AND the full running firmware image must match.
    control_armed.store(false);
    uint8_t running_image_sha256[32]{};
    const bool valid_image=control_current_image_sha256(
        running_image_sha256);
    const esp_partition_t* running=esp_ota_get_running_partition();
    esp_ota_img_states_t image_state=ESP_OTA_IMG_UNDEFINED;
    const bool pending_ota=running &&
        esp_ota_get_state_partition(running,&image_state)==ESP_OK &&
        image_state==ESP_OTA_IMG_PENDING_VERIFY;
    const bool activating_ota=pending_ota ||
        cottage_ota_confirmation_pending();

    Preferences prefs;
    bool restore=false;
    bool must_revoke=false;
    if(prefs.begin("cottage_ctl",true)) {
        StoredControlIntent saved{};
        const size_t stored_size=prefs.getBytesLength("intent1");
        if(stored_size==sizeof(saved) &&
           prefs.getBytes("intent1",&saved,sizeof(saved))==sizeof(saved)) {
            restore=cn105_selected() && valid_image && !activating_ota &&
                control_intent_restorable(saved,site_settings_snapshot(),
                                          running_image_sha256);
            must_revoke=saved.enabled!=0 && !restore;
        } else if(stored_size!=0) {
            // An older / incompatible authorization is never inherited.
            must_revoke=true;
        }
        prefs.end();
    }
    control_armed_endpoint.store(site_settings_endpoint_generation());
    control_reset_mode_history();
    if(must_revoke && !cottage_control_revoke_saved_intent())
        Serial.println("CONTROL_WARN: stale authorization revoke failed");
    control_armed.store(restore && !must_revoke);
    web.on("/api/control/arm",HTTP_POST,[]() {
        if(!cottage_wifi_require_home_lan(web))return;
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
        const bool midea=cottage_midea_selected();
        if(web.hasArg("connection") && web.arg("connection")!=control_connection_key(midea)) {
            web.send(409,"application/json","{\"saved\":false,\"error\":\"CONNECTION_CHANGED\"}");
            return;
        }
        const bool gree=cottage_gree_selected();
        const bool daikin=cottage_daikin_selected();
        const bool wf_rac=cottage_wf_rac_selected();
        const bool direct_cn105=cottage_cn105_direct_selected();
        const bool s21=cottage_daikin_s21_selected();
        const bool haier=cottage_haier_selected();
        const bool panasonic=cottage_panasonic_selected();
        if(requested && !midea && !gree && !daikin && !wf_rac && !direct_cn105 && !s21 && !haier && !panasonic && !cn105_selected()) {
            web.send(409,"application/json",
                "{\"saved\":false,\"error\":\"ADAPTER_READ_ONLY\"}");
            return;
        }
        uint8_t digest[32]{};
        // Never grant authorization when firmware identity cannot be read,
        // or when a new OTA image still awaits confirmation.
        const esp_partition_t* running=esp_ota_get_running_partition();
        esp_ota_img_states_t image_state=ESP_OTA_IMG_UNDEFINED;
        const bool pending_ota=running &&
            esp_ota_get_state_partition(running,&image_state)==ESP_OK &&
            image_state==ESP_OTA_IMG_PENDING_VERIFY;
        const bool allowed=requested ?
            control_current_image_sha256(digest) &&
            !pending_ota && !cottage_ota_confirmation_pending() : true;
        if(midea || gree || daikin || wf_rac || direct_cn105 || s21 || haier || panasonic) {
            bool ready=true;
            if(requested && allowed) {
                if(midea)ready=cottage_midea_automatic_ready();
                else if(gree)ready=cottage_gree_automatic_ready();
                else if(daikin)ready=cottage_daikin_automatic_ready();
                else if(wf_rac)ready=cottage_wf_rac_automatic_ready();
                else if(direct_cn105)ready=cottage_cn105_direct_automatic_ready();
                else if(s21)ready=cottage_daikin_s21_automatic_ready();
                else if(haier)ready=cottage_haier_automatic_ready();
                else ready=cottage_panasonic_automatic_ready();
            }
            if(requested && (!allowed || !ready)) {
                web.send(409,"application/json",midea?
                    "{\"saved\":false,\"error\":\"MIDEA_NOT_READY\"}":gree?
                    "{\"saved\":false,\"error\":\"GREE_NOT_READY\"}":
                    daikin?"{\"saved\":false,\"error\":\"DAIKIN_NOT_READY\"}":
                        wf_rac?"{\"saved\":false,\"error\":\"WF_RAC_NOT_READY\"}":
                            direct_cn105?"{\"saved\":false,\"error\":\"CN105_DIRECT_NOT_READY\"}":
                                s21?"{\"saved\":false,\"error\":\"DAIKIN_S21_NOT_READY\"}":
                                    haier?"{\"saved\":false,\"error\":\"HAIER_NOT_READY\"}":
                                        "{\"saved\":false,\"error\":\"PANASONIC_NOT_READY\"}");
                return;
            }
            // LAN pilot approval is volatile and bound to this enrollment.
            control_permission_generation.fetch_add(1);
            control_armed_endpoint.store(site_settings_endpoint_generation());
            if(midea)control_armed_midea_revision.store(cottage_midea_configuration_generation());
            else if(gree)control_armed_gree_revision.store(cottage_gree_configuration_generation());
            else if(daikin) {
                control_armed_daikin_revision.store(cottage_daikin_configuration_generation());
                control_armed_daikin_wifi.store(cottage_wifi_link_generation());
            } else if(wf_rac) {
                control_armed_wf_rac_revision.store(cottage_wf_rac_configuration_generation());
                control_armed_wf_rac_wifi.store(cottage_wifi_link_generation());
            } else if(direct_cn105) {
                control_armed_cn105_direct_revision.store(cottage_cn105_direct_configuration_generation());
                control_armed_cn105_bus.store(cottage_cn105_direct_bus_generation());
            } else if(s21) {
                control_armed_s21_revision.store(cottage_daikin_s21_configuration_generation());
                control_armed_s21_bus.store(cottage_daikin_s21_bus_generation());
            } else if(haier) {
                control_armed_haier_revision.store(cottage_haier_configuration_generation());
                control_armed_haier_bus.store(cottage_haier_bus_generation());
            } else {
                control_armed_panasonic_revision.store(cottage_panasonic_configuration_generation());
                control_armed_panasonic_bus.store(cottage_panasonic_bus_generation());
            }
            control_armed.store(requested);
            web.send(200,"application/json","{\"saved\":true}");
            return;
        }
        const auto record=control_intent_for(
            site_settings_snapshot(),requested && allowed,digest);
        // Exact pump/adapter AND signed/USB image identity together.
        // Reclaim only re-fetchable forecast/price caches on an explicitly
        // authorized user request when durable NVS authorization cannot fit.
        const bool stored=allowed && control_save_user_arm(record);
        if(stored) {
            control_armed_endpoint.store(site_settings_endpoint_generation());
            // Arming starts the dwell window: mode history while disarmed
            // is unknown (manual remote changes are not observed).
            control_reset_mode_history();
            control_permission_generation.fetch_add(1);
            control_armed.store(requested);
        }
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
// ---- Building identification persistence (NVS "cottage_mdl"/"rls1") -------
// Bound to the pump adapter, pump endpoint and weather location: a changed
// pump or site restarts learning instead of reusing another building's model.
static bool variable_setpoint_model_selected() {
    const auto cfg=c23_mode_config();
    ComfortBand legacy{};
    return direct_lan_pump_selected() || cfg.mode==CottageMode::Occupied ||
        (cfg.mode==CottageMode::Hold && !mpc_comfort_band(cfg.hold_target_c,legacy));
}
static const char* model_blob_key(bool variable) {return variable?"rls2":"rls1";}
static uint32_t identifier_identity() {
    const auto s=site_settings_snapshot();
    uint32_t h=2166136261u;
    const auto mix=[&](const char* p,size_t n) {
        for(size_t i=0;i<n && p[i];++i) {h^=static_cast<uint8_t>(p[i]);h*=16777619u;}
        h^=0xFFu;h*=16777619u;
    };
    mix(s.pump_adapter,sizeof(s.pump_adapter));
    mix(s.cn105_url,sizeof(s.cn105_url));
    mix(s.fmi_latlon,sizeof(s.fmi_latlon));
    // The model describes the room the bound indoor sensor measures.
    const auto room=site_room_temperature_config();
    const uint32_t sensor=room.source==RoomTemperatureSource::Ruuvi?c23_indoor_binding_id():0;
    for(int i=0;i<4;++i) {h^=(sensor>>(8*i))&0xFFu;h*=16777619u;}
    // Preserve the identity of calibrations from the default uncorrected Ruuvi.
    if(room.source!=RoomTemperatureSource::Ruuvi || room.correction_c100!=0) {
        h^=static_cast<uint8_t>(room.source);h*=16777619u;
        const uint16_t correction=static_cast<uint16_t>(room.correction_c100);
        h^=correction&0xFFu;h*=16777619u;
        h^=correction>>8;h*=16777619u;
    }
    if(uses_common_pump_state()) {
        uint32_t pump_id=0;
        if(cottage_midea_selected())pump_id=cottage_midea_calibration_identity();
        else if(cottage_gree_selected())pump_id=cottage_gree_calibration_identity();
        else if(cottage_daikin_selected())pump_id=cottage_daikin_calibration_identity();
        else if(cottage_daikin_dsiot_selected())pump_id=cottage_daikin_dsiot_calibration_identity();
        else if(cottage_wf_rac_selected())pump_id=cottage_wf_rac_calibration_identity();
        else if(cottage_cn105_direct_selected())pump_id=cottage_cn105_direct_calibration_identity();
        else if(cottage_daikin_s21_selected())pump_id=cottage_daikin_s21_calibration_identity();
        else if(cottage_haier_selected())pump_id=cottage_haier_calibration_identity();
        else if(cottage_panasonic_selected())pump_id=cottage_panasonic_calibration_identity();
        for(int i=0;i<4;++i) {h^=(pump_id>>(8*i))&0xFFu;h*=16777619u;}
    }
    return h;
}
static std::atomic<bool> identifier_reset_requested{false};
static bool identifier_load(BuildingIdentifier& id,uint32_t identity) {
    Preferences prefs;
    if(!prefs.begin("cottage_mdl",true)) return false;
    const char* key=model_blob_key(id.variable_setpoints());
    const size_t n=prefs.getBytesLength(key);
    bool ok=false;
    if(n>0 && n<=BuildingIdentifier::blob_capacity) {
        auto* buf=static_cast<uint8_t*>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(buf) {
            ok=prefs.getBytes(key,buf,n)==n && id.load(identity,buf,n);
            heap_caps_free(buf);
        }
    }
    prefs.end();
    return ok;
}
enum class ModelStore : uint8_t { Stored, SkippedLowSpace, Failed };
// The 20-KiB NVS partition also holds Wi-Fi, the signed-OTA journal and the
// arming record. The model (about 1.3 KiB) is written only while plenty of
// entries stay free: an NVS blob update needs room for the new copy before
// the old one is erased. Losing a few hours of learning is acceptable;
// crowding out those records is not.
static ModelStore identifier_store(const BuildingIdentifier& id,uint32_t identity) {
    constexpr size_t cap=BuildingIdentifier::blob_capacity;
    auto* buf=static_cast<uint8_t*>(heap_caps_malloc(cap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!buf) return ModelStore::Failed;
    size_t used=0;
    bool ok=id.save(identity,buf,cap,used);
    nvs_stats_t stats{};
    const size_t entries=(used+31)/32+4;
    if(ok && (nvs_get_stats(nullptr,&stats)!=ESP_OK || stats.free_entries<2*entries+128)) {
        heap_caps_free(buf);
        return ModelStore::SkippedLowSpace;
    }
    if(ok) {
        nvs_handle_t handle=0;
        esp_err_t err=nvs_open("cottage_mdl",NVS_READWRITE,&handle);
        if(err==ESP_OK) {
            err=nvs_set_blob(handle,model_blob_key(id.variable_setpoints()),buf,used);
            if(err==ESP_OK) err=nvs_commit(handle);
            nvs_close(handle);
        }
        ok=err==ESP_OK;
    }
    heap_caps_free(buf);
    return ok?ModelStore::Stored:ModelStore::Failed;
}
// NVS side of the verifiable model reset (cottage/model_persistence.hpp):
// the learned model is bound to a persistent "reset epoch" so that a failed
// erase can never bring the old calibration back after a reboot.
struct NvsModelStore {
    bool variable=false;
    const char* epoch_key() const {return variable?"epoch2":"epoch";}
    EpochRead read_epoch(uint32_t& out) {
        nvs_handle_t handle=0;
        esp_err_t err=nvs_open("cottage_mdl",NVS_READONLY,&handle);
        if(err==ESP_ERR_NVS_NOT_FOUND) return EpochRead::Missing;   // namespace never created
        if(err!=ESP_OK) return EpochRead::Error;
        uint32_t value=0;
        err=nvs_get_u32(handle,epoch_key(),&value);
        nvs_close(handle);
        if(err==ESP_OK) {out=value;return EpochRead::Ok;}
        return err==ESP_ERR_NVS_NOT_FOUND?EpochRead::Missing:EpochRead::Error;
    }
    bool write_epoch(uint32_t value) {
        nvs_handle_t handle=0;
        if(nvs_open("cottage_mdl",NVS_READWRITE,&handle)!=ESP_OK) return false;
        esp_err_t err=nvs_set_u32(handle,epoch_key(),value);
        if(err==ESP_OK) err=nvs_commit(handle);
        nvs_close(handle);
        return err==ESP_OK;
    }
    bool erase_model() {
        nvs_handle_t handle=0;
        if(nvs_open("cottage_mdl",NVS_READWRITE,&handle)!=ESP_OK) return false;
        esp_err_t err=nvs_erase_key(handle,model_blob_key(variable));
        bool ok=err==ESP_OK || err==ESP_ERR_NVS_NOT_FOUND;
        if(err==ESP_OK) ok=nvs_commit(handle)==ESP_OK;
        nvs_close(handle);
        return ok;
    }
    bool model_present() {                    // true unless verified absent
        nvs_handle_t handle=0;
        esp_err_t err=nvs_open("cottage_mdl",NVS_READONLY,&handle);
        if(err==ESP_ERR_NVS_NOT_FOUND) return false;
        if(err!=ESP_OK) return true;
        size_t n=0;
        err=nvs_get_blob(handle,model_blob_key(variable),nullptr,&n);
        nvs_close(handle);
        return err!=ESP_ERR_NVS_NOT_FOUND;
    }
};
// Outdoor temperature for the slot starting at `utc` from the cached MET
// Norway hourly points (as-of value; no outdoor sensor exists).
static bool weather_at(const enevina::OutdoorPoint* w,size_t n,int64_t utc,double& out) {
    for(size_t i=0;i+1<n;++i) {
        if(w[i].timestamp_utc<=utc && w[i+1].timestamp_utc>utc) {
            if(w[i+1].timestamp_utc-w[i].timestamp_utc!=3600 ||
               !std::isfinite(w[i].temperature_c) || !std::isfinite(w[i+1].temperature_c))
                return false;
            const double f=double(utc-w[i].timestamp_utc)/3600.0;
            out=w[i].temperature_c+f*(w[i+1].temperature_c-w[i].temperature_c);
            return out>=-55 && out<=45;
        }
    }
    return false;
}
// One 15-minute slot being observed for identification.
struct LearningSlot {
    bool open=false;
    int64_t utc=0;
    bool indoor_ok=false;double indoor_c=0;
    bool outdoor_ok=false;double outdoor_c=0;
    uint32_t commands_sent=0;
    uint32_t config_generation=0;
    bool energy_ok=false;double total_wh=0;int64_t energy_utc=0;
};
static void optimizer_worker(void*) {
    auto* prices=new enevina_firmware::PricePoint[200];
    auto* weather=new enevina::OutdoorPoint[64];
    auto* aligned=new Spot15[200];
    // Large planning buffers in PSRAM, never on the 16-KiB task stack.
    auto* hz=static_cast<HorizonResult*>(heap_caps_malloc(sizeof(HorizonResult),
        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto* rows=static_cast<PlanRow*>(heap_caps_malloc(sizeof(PlanRow)*max_horizon_slots,
        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto* legacy=new (std::nothrow) Row[max_horizon_slots];
    auto* trend=static_cast<TrendPoint*>(heap_caps_malloc(
        sizeof(TrendPoint)*BuildingIdentifier::trend_slots,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    void* id_mem=heap_caps_malloc(sizeof(BuildingIdentifier),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    const bool initial_variable=variable_setpoint_model_selected();
    BuildingIdentifier* id=id_mem?new(id_mem) BuildingIdentifier(initial_variable):nullptr;
    if(hz) new(hz) HorizonResult();
    if(rows) for(size_t i=0;i<max_horizon_slots;++i) new(rows+i) PlanRow();
    uint32_t identity=identifier_identity();
    // The stored model is bound to site identity AND the persistent reset
    // epoch; an unreadable epoch means the blob may be a reset-away model.
    uint32_t model_epoch=0;
    NvsModelStore model_store{initial_variable};
    bool epoch_known=load_model_epoch(model_store,model_epoch);
    const bool restored=id && epoch_known &&
        identifier_load(*id,model_store_identity(identity,model_epoch));
    bool reset_unpersisted=false,reset_by_owner=false;
    uint32_t electric_meter_id=0;
    uint32_t last_reset_retry_ms=0;
    if(id) {
        xSemaphoreTake(lock_data,portMAX_DELAY);
        identifier=id;mpc_rows=rows;mpc_trend=trend;identifier_restored=restored;
        calibration_snapshot=id->report(time(nullptr));
        xSemaphoreGive(lock_data);
    }
    LearningSlot slot{};
    int64_t last_boundary=0;
    uint32_t saved_revision=id?id->report(0).revision:0;
    unsigned boundaries_since_save=0;
    for(;;) {
        const int64_t now=time(nullptr);
        const int64_t slot_start=(now/900)*900;
        bool boundary=false;
        if(id) {
            // Pump/site/sensor identity change or an explicit owner reset:
            // start learning anew. The RAM model is reset at once; the NVS
            // reset is verified (blob gone or epoch advanced) and, if it
            // cannot be, retried and REPORTED as not persisted - the owner is
            // never told a reset is permanent while the old calibration could
            // still come back after a reboot (Codex P2, PR #63).
            const uint32_t current_identity=identifier_identity();
            const bool owner_reset=identifier_reset_requested.exchange(false);
            const bool variable=variable_setpoint_model_selected();
            const bool domain_changed=variable!=id->variable_setpoints();
            if(domain_changed && !reset_unpersisted) {
                // Keep the other operating range's calibration in its own NVS
                // record. Never train or restore one domain from the other.
                identity=current_identity;
                id->~BuildingIdentifier();
                new(id) BuildingIdentifier(variable);
                model_store.variable=variable;
                epoch_known=load_model_epoch(model_store,model_epoch);
                const bool domain_restored=epoch_known && !owner_reset &&
                    identifier_load(*id,model_store_identity(identity,model_epoch));
                reset_unpersisted=false;reset_by_owner=false;
                slot=LearningSlot{};
                saved_revision=id->report(now).revision;
                boundaries_since_save=0;
                xSemaphoreTake(lock_data,portMAX_DELAY);
                identifier_restored=domain_restored;
                mpc_plan=MpcPlanRecord{};
                plan_count=0;
                last_plan.status=MpcStatus::InvalidInput;
                calibration_snapshot=id->report(now);
                xSemaphoreGive(lock_data);
            }
            if(owner_reset || (variable==id->variable_setpoints() && current_identity!=identity)) {
                identity=current_identity;
                id->reset();
                reset_unpersisted=persist_model_reset(model_store,model_epoch,epoch_known)!=
                    ModelResetResult::Persisted;
                slot=LearningSlot{};
                saved_revision=0;
                boundaries_since_save=0;
                reset_by_owner=owner_reset;
                if(owner_reset) identifier_reset_state.store(reset_unpersisted?3:2);
                if(reset_unpersisted) identifier_reset_failures.fetch_add(1);
                last_reset_retry_ms=millis();
                xSemaphoreTake(lock_data,portMAX_DELAY);
                identifier_restored=false;
                mpc_plan=MpcPlanRecord{};
                plan_count=0;
                last_plan.status=MpcStatus::InvalidInput;
                calibration_snapshot=id->report(now);
                xSemaphoreGive(lock_data);
            } else if(reset_unpersisted &&
                      static_cast<uint32_t>(millis()-last_reset_retry_ms)>=60000u) {
                last_reset_retry_ms=millis();
                reset_unpersisted=persist_model_reset(model_store,model_epoch,epoch_known)!=
                    ModelResetResult::Persisted;
                if(reset_by_owner) identifier_reset_state.store(reset_unpersisted?3:2);
            }
        }
        if(id) {
            const uint32_t meter_id=shelly_meter_pump_only()?shelly_meter_identity():0;
            if(meter_id!=electric_meter_id) {
                electric_meter_id=meter_id;
                id->reset_electric();
            }
        }
        if(wifi_ok.load() && clock_ok() && prices && weather && aligned && hz && rows &&
           legacy && trend && id && id->variable_setpoints()==variable_setpoint_model_selected()) {
            // A fresh indoor measurement is not a forecast for an entire
            // unfinished quarter-hour: learn and plan only at the boundary.
            boundary=(now-slot_start)<=60 && slot_start!=last_boundary;
        }
        CN105Telemetry cp;TemperatureSample ts;ShellyTelemetry meter;
        xSemaphoreTake(lock_data,portMAX_DELAY);
        cp=pump;meter=consumption;
        xSemaphoreGive(lock_data);
        const uint32_t optimizer_site_generation=site_settings_generation();
        const bool midea=cottage_midea_selected();
        const auto view=boundary?optimizer_pump_state(now,midea):enevina::PumpRoomState{};
        const bool view_heat=view.mode==enevina::PumpRoomMode::Heat;
        ts=selected_room_sample(now);
        if(site_settings_generation()!=optimizer_site_generation ||
           (id && identifier_identity()!=identity))ts={};
        const auto ms=enevina_firmware::market_status();
        const auto ws=enevina_firmware::weather_status();
        const size_t pc=boundary?enevina_firmware::future_prices(prices,200):0;
        const size_t wc=boundary?enevina_firmware::future_weather(weather,64):0;
        if(boundary) {
            last_boundary=slot_start;
            // (1) Close the previous slot: one identification observation.
            if(slot.open && slot.utc==slot_start-900 &&
               slot.config_generation==c23_config_generation() &&
               optimizer_site_generation==site_settings_generation()) {
                SlotObservation o{};
                o.slot_utc=slot.utc;
                o.indoor_valid=slot.indoor_ok && valid_temperature(ts,now);
                o.indoor_start_c=slot.indoor_c;o.indoor_end_c=ts.indoor_c;
                o.outdoor_valid=slot.outdoor_ok;o.outdoor_c=slot.outdoor_c;
                // The verified pump mode at the slot end, held uninterrupted
                // (source-verified polls, gaps reset the duration) since at
                // most ~1 min after the slot began, and no command sent
                // during the slot. A gateway receipt never labels data.
                o.heat_valid=known_heating_mode(view) && view.request_minutes>=14 &&
                    control_commands_sent.load()==slot.commands_sent;
                o.heat_requested=view_heat;
                o.setpoint_c=view.setpoint_c;
                o.in_model_domain=id->variable_setpoints() || !view_heat ||
                    view.setpoint_c<=16.01;
                const int64_t dt=meter.captured_utc-slot.energy_utc;
                o.electric_valid=slot.energy_ok && valid_shelly(meter,now,60) &&
                    meter.total_wh>=slot.total_wh && dt>=600 && dt<=1200;
                if(o.electric_valid)
                    o.electric_kwh=(meter.total_wh-slot.total_wh)/1000.0*900.0/double(dt);
                o.meter_pump_only=shelly_meter_pump_only();
                id->add(o);
            }
            slot.open=false;
            // (2) Persist at most every 6 h, only when the model changed.
            const uint32_t revision=id->report(now).revision;
            if(++boundaries_since_save>=24 && revision!=saved_revision) {
                boundaries_since_save=0;
                if(!epoch_known) epoch_known=load_model_epoch(model_store,model_epoch);
                const ModelStore stored=epoch_known?
                    identifier_store(*id,model_store_identity(identity,model_epoch)):
                    ModelStore::Failed;
                if(stored==ModelStore::Stored) saved_revision=revision;
                xSemaphoreTake(lock_data,portMAX_DELAY);
                if(stored==ModelStore::Stored) identifier_nvs_saves++;
                else if(stored==ModelStore::SkippedLowSpace) identifier_nvs_skipped++;
                else identifier_nvs_failures++;
                xSemaphoreGive(lock_data);
            }
            // (3) Verified receding horizon: longest priced + forecast run.
            for(size_t i=0;i<pc;i++) aligned[i]={prices[i].start_utc,prices[i].spot_eur_per_kwh};
            *hz=HorizonResult{};
            if(pc && wc)
                *hz=join_forecast_horizon(now,ms.last_fetch_utc,ws.fetched_utc,aligned,pc,
                    weather,wc,bridge_slot_price,nullptr,max_horizon_slots,
                    mpc_min_horizon_slots);
            else hz->bridge.status=pc?BridgeStatus::StaleWeather:BridgeStatus::StalePrices;
            // (4) Plan with the identified model; only slot 0 is ever used.
            bool heating_allowed=false;double target_c=0;
            mpc_mode_target(heating_allowed,target_c);
            PlannerInput pi{};
            pi.now_utc=now;pi.identifier=id;
            pi.meter_pump_only=shelly_meter_pump_only();
            pi.currency_scale=currency_limit_scale(ms.currency);
            pi.heating_allowed=heating_allowed;pi.target_valid=heating_allowed;
            pi.target_c=target_c;
            pi.temperature_fresh=valid_temperature(ts,now);pi.indoor_c=ts.indoor_c;
            pi.pump_state_known=known_heating_mode(view);pi.pump_heat=view_heat;
            pi.pump_setpoint_c=view.setpoint_c;
            pi.pump_limits=view.heating;
            pi.minutes_in_mode=view.mode_minutes;
            pi.horizon=hz;
            pi.mode_generation=view.request_generation;
            pi.config_generation=c23_config_generation();
            pi.settings_generation=optimizer_site_generation;
            pi.limits=owner_comfort_limits();
            pi.profile=site_settings_snapshot().profile;
            pi.controls.runtime_budget_ms=1800;
            pi.controls.monotonic_now_ms=cottage_now_ms;
            pi.controls.cancel_requested=cottage_cancel;
            const bool attempt=id->calibrated(now);
            const uint32_t begun=millis();
            const PlannerOutput po=plan_mpc(pi,rows);
            const uint32_t elapsed=millis()-begun;
            // Legacy /api/plan rows mirror the MPC plan.
            std::fill(legacy,legacy+max_horizon_slots,Row{});
            for(size_t i=0;i<po.rows;++i)
                legacy[i]={rows[i].utc,rows[i].indoor_c,rows[i].energy_kwh,rows[i].cost,rows[i].heat};
            const MpcResult result=po.result;
            const auto after=optimizer_pump_state(time(nullptr),midea);
            xSemaphoreTake(lock_data,portMAX_DELAY);
            // The optimization ran without holding lock_data. A pump mode
            // transition may have invalidated its input snapshot while the
            // solver was busy; never publish that old plan, even if
            // HEAT -> OFF -> HEAT returned to the same mode.
            const bool mode_changed=after.request_generation!=view.request_generation ||
                after.mode!=view.mode || after.endpoint_generation!=view.endpoint_generation ||
                after.link_generation!=view.link_generation ||
                after.configuration_generation!=view.configuration_generation ||
                (view.source==enevina::PumpStateSource::VerifiedBus &&
                    view.request_generation!=observed_mode_generation) ||
                site_settings_generation()!=optimizer_site_generation;
            MpcPlanRecord record=po.record;
            if(mode_changed) {
                record.has_plan=false;record.planning_fallback=MpcFallback::PlanInputsChanged;
            }
            mpc_plan=record;
            mpc_row_count=record.has_plan?po.rows:0;
            mpc_energy_in_kwh=po.energy_in_kwh;
            mpc_energy_kwh=po.energy_kwh;mpc_cost=po.cost;
            mpc_uncertainty_c=po.params.planning_uncertainty_c;
            horizon_price_slots=hz->price_slots;horizon_weather_slots=hz->weather_slots;
            horizon_slots=hz->bridge.count;horizon_limit=hz->limit;
            bridge_status=hz->bridge.status;
            calibration_snapshot=id->report(now);
            mpc_trend_count=trend?id->trend(trend,BuildingIdentifier::trend_slots):0;
            if(attempt && record.status!=MpcStatus::InvalidInput) {
                mpc_solves++;mpc_solve_ms=elapsed;
                mpc_solve_worst_ms=std::max(mpc_solve_worst_ms,elapsed);
                if(record.status!=MpcStatus::Ok && record.status!=MpcStatus::UnsafeStart)
                    mpc_solver_failures++;
                optimizer_ms=elapsed;max_optimizer_ms=std::max(max_optimizer_ms,elapsed);
            }
            last_optimized_utc=now;
            if(mode_changed) {
                plan_count=0;
                last_plan.status=MpcStatus::InvalidInput;
            } else if(record.has_plan) {
                last_plan=result;
                std::copy(legacy,legacy+max_horizon_slots,plan_rows.begin());
                plan_count=po.rows;
            } else {
                last_plan=result;
                plan_count=0;
                last_plan.status=record.status==MpcStatus::Ok?MpcStatus::InvalidInput:
                                                                record.status;
            }
            strlcpy(reason_text,record.has_plan?"MPC-suunnitelma laskettu":
                    mpc_fallback_name(record.planning_fallback),sizeof(reason_text));
            xSemaphoreGive(lock_data);
            // Diagnostic supervisor (never authorises writes).
            Health health;
            health.now_utc=now;
            health.pump=cp;health.independent_temperature=c23_temperature_sample();
            health.watchdog_ok=(millis()-web_loop_heartbeat_ms.load()<5000);
            health.calibrated=attempt;
            health.independent_freeze_protection_verified=false;
            health.price_fresh=ms.current_price_valid;
            health.weather_fresh=ws.available;
            health.mpc_ready=record.has_plan;
            health.plan=po.result;
            const Parameters& sp=record.has_plan?po.params:model;
            const Decision verdict=supervise(sp,health);
            xSemaphoreTake(lock_data,portMAX_DELAY);
            safety=verdict;
            free_heap=ESP.getFreeHeap();free_psram=ESP.getFreePsram();
            stack_watermark=uxTaskGetStackHighWaterMark(nullptr);
            xSemaphoreGive(lock_data);
        } else if(!wifi_ok.load() || !clock_ok()) {
            xSemaphoreTake(lock_data,portMAX_DELAY);
            strlcpy(reason_text,"Wi-Fi, aika tai resurssit eivät ole valmiina",
                    sizeof(reason_text));
            xSemaphoreGive(lock_data);
        }
        cottage_control_tick();
        if(boundary) {
            // (5) Open the new slot AFTER this boundary's control decision:
            // a command sent later in the slot invalidates its observation.
            slot=LearningSlot{};
            slot.open=true;slot.utc=slot_start;
            slot.indoor_ok=valid_temperature(ts,now);slot.indoor_c=ts.indoor_c;
            slot.outdoor_ok=ws.available && weather_at(weather,wc,slot_start+450,slot.outdoor_c);
            slot.commands_sent=control_commands_sent.load();
            slot.config_generation=c23_config_generation();
            xSemaphoreTake(lock_data,portMAX_DELAY);
            meter=consumption;
            xSemaphoreGive(lock_data);
            slot.energy_ok=valid_shelly(meter,time(nullptr),30);
            slot.total_wh=meter.total_wh;slot.energy_utc=meter.captured_utc;
        }
        // Recompute on quarter-hour boundary, retries every minute when offline.
        const time_t t=time(nullptr);
        int sleep_s=clock_ok()?static_cast<int>(900-(t%900)):60;
        if(sleep_s>60)sleep_s=60; // Allows freshness monitoring during long slots.
        vTaskDelay(pdMS_TO_TICKS(std::max(5,sleep_s)*1000));
    }
}
static void add_control_phase(cJSON* root,const char* key,const MitsubishiCommandPhase& phase) {
    if(phase.utc<=0 || phase.step==ControlStep::None)return;
    if(auto* entry=cJSON_AddObjectToObject(root,key)) {
        cJSON_AddStringToObject(entry,"step",control_step_name(phase.step));
        cJSON_AddNumberToObject(entry,"utc",static_cast<double>(phase.utc));
        if(phase.step!=ControlStep::SetOff)cJSON_AddNumberToObject(entry,"target_c",phase.target_c);
    }
}
static void status_endpoint() {
    MitsubishiCommandReadback cn105_command{};
    CN105Telemetry cp;ShellyTelemetry st;TemperatureSample te;
    CN105Telemetry observed; // SSE-only values, never source-verified
    BridgeStatus bs;MpcResult plan;MpcStatus plan_record_status=MpcStatus::InvalidInput;Decision current_safety;
    int64_t optimized=0,climate_time=0,compressor_time=0,link_time=0;
    double link_uptime_s=0;
    bool relay_on=false;
    char reason[80];
    uint32_t ms,worst,heap,psram,stack;
    xSemaphoreTake(lock_data,portMAX_DELAY);
    cp=pump;observed=sse_observed_pump;
    if(cn105_command_link.current())cn105_command=cn105_command_readback;
    st=consumption;/* C2.3 temperature read outside lock */bs=bridge_status;
    relay_on=shelly_relay_on;
    current_safety=safety;
    climate_time=climate_utc;
    compressor_time=compressor_utc;
    link_time=cn105_link_event_utc;
    link_uptime_s=cn105_link_uptime_s;
    plan=last_plan;optimized=last_optimized_utc;
    plan_record_status=mpc_plan.has_plan?mpc_plan.status:MpcStatus::InvalidInput;
    strlcpy(reason,reason_text,sizeof(reason));
    ms=optimizer_ms;worst=max_optimizer_ms;heap=free_heap;psram=free_psram;
    stack=stack_watermark;
    xSemaphoreGive(lock_data);
    const int64_t now=time(nullptr);
    const auto room=site_room_temperature_config();
    const auto selected=selected_room_sample(now,room);
    te=c23_temperature_sample();
    const auto price=enevina_firmware::market_status();
    const auto weather=enevina_firmware::weather_status();
    double total=0;
    const bool valid_total=price.current_price_valid &&
        site_slot_total_price((now/900)*900,price.current_spot_eur_per_kwh,total);
    const BootstrapDecision bootstrap=bootstrap_preview(now,selected);
    cJSON* root=cJSON_CreateObject();
    const bool midea=cottage_midea_selected();
    const bool armed=active_control_armed(midea);
    cJSON_AddStringToObject(root,"mode",armed?
        (midea?"MIDEA_AUTOMAATIO_KAYTOSSA":cottage_gree_selected()?
            "GREE_AUTOMAATIO_KAYTOSSA":cottage_daikin_selected()?"DAIKIN_AUTOMAATIO_KAYTOSSA":
            cottage_wf_rac_selected()?"WF_RAC_AUTOMAATIO_KAYTOSSA":cottage_cn105_direct_selected()?
            "SUORA_CN105_AUTOMAATIO_KAYTOSSA":cottage_daikin_s21_selected()?"DAIKIN_S21_AUTOMAATIO_KAYTOSSA":
                cottage_haier_selected()?"HAIER_AUTOMAATIO_KAYTOSSA":
                cottage_panasonic_selected()?"PANASONIC_AUTOMAATIO_KAYTOSSA":
            "CN105_AUTOMAATIO_KAYTOSSA"):"VARJOTILA_EI_KOMENTOJA");
    cJSON_AddBoolToObject(root,"physical_writes",armed);
    cJSON_AddBoolToObject(root,"control_armed",armed);
    cJSON_AddStringToObject(root,"control_connection",control_connection_key(midea).c_str());
    cJSON_AddNumberToObject(root,"control_arm_nvs_reclaimed",
                            control_arm_nvs_reclaimed.load());
    cJSON_AddNumberToObject(root,"control_arm_nvs_save_failures",
                            control_arm_nvs_save_failures.load());
    cJSON_AddNumberToObject(root,"control_arm_nvs_last_error",
                            control_arm_nvs_last_error.load());
    cJSON_AddStringToObject(root,"control_gate",
        control_gate_name(static_cast<ControlBlock>(control_gate.load())));
    cJSON_AddStringToObject(root,"control_last_step",
        control_step_name(static_cast<ControlStep>(control_last_step.load())));
    if(!midea && cn105_selected()) {
        add_control_phase(root,"cn105_control_requested",cn105_command.requested);
        add_control_phase(root,"cn105_control_sent",cn105_command.sent);
        add_control_phase(root,"cn105_control_confirmed",cn105_command.confirmed);
    }
    cJSON_AddNumberToObject(root,"control_commands_sent",control_commands_sent.load());
    cJSON_AddNumberToObject(root,"control_commands_confirmed",control_commands_confirmed.load());
    cJSON_AddNumberToObject(root,"control_commands_failed",control_commands_failed.load());
    cJSON_AddNumberToObject(root,"control_post_stage",control_post_stage.load());
    cJSON_AddNumberToObject(root,"control_post_http_code",control_post_http_code.load());
    cJSON_AddNumberToObject(root,"control_get_http_code",control_get_http_code.load());
    cJSON_AddNumberToObject(root,"cn105_get_retry_count",cn105_get_retry_count.load());
    cJSON_AddNumberToObject(root,"cn105_get_recovered_count",cn105_get_recovered_count.load());
    cJSON_AddNumberToObject(root,"cn105_get_timeout_count",cn105_get_timeout_count.load());
    cJSON_AddNumberToObject(root,"cn105_get_lease_denials",
                            cn105_get_lease_denials.load());
    cJSON_AddNumberToObject(root,"cn105_get_lease_low_memory",
                            cn105_get_lease_low_memory.load());
    cJSON_AddNumberToObject(root,"cn105_get_lease_busy",
                            cn105_get_lease_busy.load());
    cJSON_AddNumberToObject(root,"cn105_get_last_free_internal",
                            cn105_get_last_free_internal.load());
    cJSON_AddNumberToObject(root,"cn105_get_last_largest_internal",
                            cn105_get_last_largest_internal.load());
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
    cJSON_AddBoolToObject(root,"bootstrap_price_data_valid",
                             bootstrap.price_data_valid);
    if(bootstrap.price_data_valid) {
        cJSON_AddNumberToObject(root,"bootstrap_total_now",
                                bootstrap.current_total_eur_per_kwh);
        cJSON_AddNumberToObject(root,"bootstrap_median_2h",
                                bootstrap.compared_median_eur_per_kwh);
    } else {
        cJSON_AddNullToObject(root,"bootstrap_total_now");
        cJSON_AddNullToObject(root,"bootstrap_median_2h");
    }
    {   // The profile that shaped the decision above (never authorizes a write).
        const SiteSettings profile_site=site_settings_snapshot();
        cJSON_AddStringToObject(root,"optimization_profile",profile_name(profile_site.profile));
        cJSON_AddStringToObject(root,"shelly_scope",shelly_scope_name(profile_site.shelly_scope));
        if(profile_site.comfort_min_c>0)
            cJSON_AddNumberToObject(root,"comfort_min_c",profile_site.comfort_min_c);
        else cJSON_AddNullToObject(root,"comfort_min_c");
        if(profile_site.comfort_max_c>0)
            cJSON_AddNumberToObject(root,"comfort_max_c",profile_site.comfort_max_c);
        else cJSON_AddNullToObject(root,"comfort_max_c");
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
    cJSON_AddStringToObject(root,"room_temperature_source",
        room.source==RoomTemperatureSource::Pump?"pump":"ruuvi");
    cJSON_AddBoolToObject(root,"room_temperature_source_configured",
        site_room_temperature_configured());
    cJSON_AddNumberToObject(root,"room_temperature_correction_c",
        static_cast<double>(room.correction_c100)/100.0);
    if(selected.received) {
        cJSON_AddNumberToObject(root,"room_temperature_c",selected.indoor_c);
        cJSON_AddNumberToObject(root,"room_temperature_age_s",static_cast<double>(now-selected.captured_utc));
    } else {
        cJSON_AddNullToObject(root,"room_temperature_c");
        cJSON_AddNullToObject(root,"room_temperature_age_s");
    }
    cJSON_AddBoolToObject(root,"shelly_relay_on",relay_on);
    if(valid_shelly(st,now))cJSON_AddNumberToObject(root,"shelly_w",st.watts);
    else cJSON_AddNullToObject(root,"shelly_w");
    if(price.current_price_valid)cJSON_AddNumberToObject(root,"spot",price.current_spot_eur_per_kwh);
    else cJSON_AddNullToObject(root,"spot");
    if(weather.available)cJSON_AddNumberToObject(root,"weather_c",weather.next_temperature_c);
    else cJSON_AddNullToObject(root,"weather_c");
    if(valid_total)cJSON_AddNumberToObject(root,"total_price",total);
    else cJSON_AddNullToObject(root,"total_price");
    // Every price above is in `price_currency` per kWh for `price_area`.
    cJSON_AddStringToObject(root,"price_currency",price.currency);
    cJSON_AddStringToObject(root,"price_area",price.area);
    cJSON_AddNumberToObject(root,"price_error",price.last_error);
    cJSON_AddNumberToObject(root,"price_http_status",price.last_http_status);
    cJSON_AddNumberToObject(root,"price_last_attempt_utc",
                            static_cast<double>(price.last_attempt_utc));
    cJSON_AddNumberToObject(root,"bridge_status",static_cast<int>(bs));
    // The MPC record the controller uses, not the legacy copy cleared on every verified mode change.
    cJSON_AddNumberToObject(root,"mpc_status",static_cast<int>(plan_record_status));
    cJSON_AddNumberToObject(root,"mpc_ms",ms);
    cJSON_AddNumberToObject(root,"mpc_worst_ms",worst);
    cJSON_AddNumberToObject(root,"optimized_utc",static_cast<double>(optimized));
    cJSON_AddNumberToObject(root,"free_heap",heap);
    cJSON_AddNumberToObject(root,"free_psram",
                            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root,"stack_free_bytes",stack);
    cJSON_AddStringToObject(root,"reason",reason);
    cJSON_AddNumberToObject(root,"bad_reads",failed_reads.load());
    {   // Live MPC chain: which controller would act now, and why.
        const auto inputs=build_arbiter_input(now,selected);
        const auto arb=arbitrate(inputs);
        const auto& mp=inputs.plan;
        xSemaphoreTake(lock_data,portMAX_DELAY);
        const auto cal=calibration_snapshot;
        xSemaphoreGive(lock_data);
        const int64_t current_slot=(now/900)*900;
        cJSON_AddBoolToObject(root,"model_calibrated",cal.state==CalibrationState::Calibrated);
        cJSON_AddStringToObject(root,"calibration_state",calibration_state_name(cal.state));
        cJSON_AddStringToObject(root,"calibration_block",calibration_block_name(cal.block));
        cJSON_AddStringToObject(root,"control_source",proposal_source_name(arb.source));
        cJSON_AddStringToObject(root,"mpc_state",mpc_run_state_name(arb.mpc_state));
        cJSON_AddStringToObject(root,"mpc_fallback",mpc_fallback_name(arb.fallback));
        if(mp.has_plan) cJSON_AddNumberToObject(root,"mpc_plan_computed_utc",
                                                static_cast<double>(mp.computed_utc));
        else cJSON_AddNullToObject(root,"mpc_plan_computed_utc");
        // Executed = the pump bridge read back this slot's planned state.
        cJSON_AddBoolToObject(root,"mpc_decision_confirmed",
            arb.source==ProposalSource::Mpc && mp.first_utc==current_slot &&
            mpc_state_read_back(inputs));
        cJSON_AddBoolToObject(root,"shelly_meter_pump_only",shelly_meter_pump_only());
        cJSON_AddBoolToObject(root,"mpc_pump_source_verified",
            valid_cn105(cp,now) && cp.evidence.source_time_verified);
    }
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
    {   // last SUCCESSFUL update, from the persisted event log (absent = unknown)
        uint32_t when=0,version=0;
        if(cottage_ota_last_successful_update(when,version) ||
           enevina_firmware::history_last_event(
               static_cast<uint8_t>(enevina::cottage::HistoryEventKind::UpdateConfirmed),when,version)) {
            cJSON_AddNumberToObject(root,"ota_last_update_utc",when);
            cJSON_AddNumberToObject(root,"ota_last_update_version",version);
        } else {
            cJSON_AddNullToObject(root,"ota_last_update_utc");
            cJSON_AddNullToObject(root,"ota_last_update_version");
        }
    }
    cJSON_AddNumberToObject(root,"ota_signed_good_version",cottage_ota_good_version());
    cJSON_AddNumberToObject(root,"ota_signed_candidate",cottage_ota_last_candidate());
    cJSON_AddNumberToObject(root,"ota_signed_status",cottage_ota_status());
    cJSON_AddBoolToObject(root,"ota_network_maintenance",
                           cottage_ota_network_maintenance());
    cJSON_AddNumberToObject(root,"ota_downloaded_bytes",
                            cottage_ota_downloaded_bytes());
    cJSON_AddNumberToObject(root,"ota_chunk_count",
                            cottage_ota_chunk_count());
    cJSON_AddNumberToObject(root,"ota_idle_yields",
                            cottage_ota_idle_yields());
    cJSON_AddNumberToObject(root,"ota_last_check_utc",cottage_ota_last_check_utc());
    cJSON_AddBoolToObject(root,"ota_dashboard_pending",
                          cottage_ota_dashboard_request_pending());
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
    cJSON_AddNumberToObject(root,"history_stack_free",
                            task_history?uxTaskGetStackHighWaterMark(task_history):0);
    cJSON_AddNumberToObject(root,"ota_stack_free",
                            task_ota?uxTaskGetStackHighWaterMark(task_ota):0);
    cJSON_AddNumberToObject(root,"midea_stack_free",
                            task_midea?uxTaskGetStackHighWaterMark(task_midea):0);
    cJSON_AddNumberToObject(root,"weather_stack_free",
                            enevina_firmware::weather_reader_stack_free());
    cJSON_AddNumberToObject(root,"market_stack_free",
                            enevina_firmware::market_reader_stack_free());
    // This handler runs in the Arduino loop task (8 KiB stack): its own
    // high-water mark covers /api/status and /api/mpc serialization.
    cJSON_AddNumberToObject(root,"loop_stack_free",uxTaskGetStackHighWaterMark(nullptr));
    if(WiFi.status()==WL_CONNECTED)cJSON_AddNumberToObject(root,"wifi_rssi_dbm",WiFi.RSSI());
    else cJSON_AddNullToObject(root,"wifi_rssi_dbm");
    c23_append_status(root);
    char* serialized=cJSON_PrintUnformatted(root);
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8",serialized?serialized:"{}");
    cJSON_free(serialized);cJSON_Delete(root);
}
static void plan_endpoint() {
    // The SAME plan record and rows /api/mpc serves (not a separate legacy
    // copy that the pump's own acknowledged transition would clear), so both
    // endpoints share one validity (Codex P2, PR #63).
    struct PsramFree {void operator()(void* x) const {heap_caps_free(x);}};
    std::unique_ptr<PlanRow,PsramFree> rows_mem(static_cast<PlanRow*>(heap_caps_malloc(
        sizeof(PlanRow)*max_horizon_slots,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
    xSemaphoreTake(lock_data,portMAX_DELAY);
    const MpcPlanRecord record=mpc_plan;
    const size_t n=(mpc_rows && rows_mem)?mpc_row_count:0;
    if(n>0 && n<=max_horizon_slots) std::copy(mpc_rows,mpc_rows+n,rows_mem.get());
    const bool calibrated=calibration_snapshot.state==CalibrationState::Calibrated;
    const bool energy=mpc_energy_in_kwh;
    const double cost=mpc_cost;
    xSemaphoreGive(lock_data);
    const int64_t current_utc=time(nullptr);
    // The same predicate the controller uses: a plan invalidated by a mode
    // change (Cold, drying block), a rebound sensor, new settings or an
    // external pump change is never shown as valid (Codex P2, PR #63).
    const auto inputs=build_arbiter_input(current_utc,selected_room_sample(current_utc));
    const MpcFallback validity=plan_validity(inputs);
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8","");
    char buf[220];
    // Only the CURRENT slot's plan from a calibrated model is ever shown:
    // a plan from an earlier slot is stale and never displayed as valid.
    const bool ok=rows_mem && calibrated && record.has_plan && record.status==MpcStatus::Ok &&
        validity==MpcFallback::None && inputs.plan.computed_utc==record.computed_utc &&
        n>0 && n<=max_horizon_slots && current_utc>=record.first_utc &&
        current_utc<record.first_utc+900;
    double minimum_c=0;
    if(ok) {
        minimum_c=rows_mem.get()[0].indoor_c;
        for(size_t i=1;i<n;i++) minimum_c=std::min(minimum_c,rows_mem.get()[i].indoor_c);
    }
    char cost_text[32];
    if(ok && energy && std::isfinite(cost)) snprintf(cost_text,sizeof(cost_text),"%.8f",cost);
    else strlcpy(cost_text,"null",sizeof(cost_text));
    snprintf(buf,sizeof(buf),
      "{\"valid\":%s,\"count\":%u,\"energy_valid\":%s,\"cost_eur\":%s,\"minimum_c\":%.5f,\"slots\":[",
      ok?"true":"false",static_cast<unsigned>(ok?n:0),ok&&energy?"true":"false",cost_text,
      minimum_c);
    web.sendContent(buf);
    for(size_t i=0;ok&&i<n;i++) {
        const PlanRow& r=rows_mem.get()[i];
        char kwh[24],eur[24];
        if(energy && std::isfinite(r.energy_kwh)) snprintf(kwh,sizeof(kwh),"%.6f",r.energy_kwh);
        else strlcpy(kwh,"null",sizeof(kwh));
        if(energy && std::isfinite(r.cost)) snprintf(eur,sizeof(eur),"%.7f",r.cost);
        else strlcpy(eur,"null",sizeof(eur));
        snprintf(buf,sizeof(buf),"%s{\"utc\":%lld,\"heat\":%s,\"setpoint_c\":%.2f,\"indoor_c\":%.5f,\"kwh\":%s,\"eur\":%s}",
            i?",":"",static_cast<long long>(r.utc),r.heat?"true":"false",
            r.setpoint_c,r.indoor_c,kwh,eur);
        web.sendContent(buf);
    }
    web.sendContent("]}");
    web.sendContent("");
}
// Full MPC / calibration view for the dashboard. Read-only snapshot.
static void json_number_or_null(char* out,size_t n,double v,const char* f,bool ok=true) {
    if(ok && std::isfinite(v)) snprintf(out,n,f,v); else strlcpy(out,"null",n);
}
static void mpc_endpoint() {
    const int64_t now=time(nullptr);
    const ArbiterInput arb_in=build_arbiter_input(now,selected_room_sample(now));
    const ArbiterOutput arb=arbitrate(arb_in);
    const MpcFallback validity=plan_validity(arb_in);
    struct PsramFree {void operator()(void* p) const {heap_caps_free(p);}};
    std::unique_ptr<PlanRow,PsramFree> rows_mem(static_cast<PlanRow*>(heap_caps_malloc(
        sizeof(PlanRow)*max_horizon_slots,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
    std::unique_ptr<TrendPoint,PsramFree> trend_mem(static_cast<TrendPoint*>(heap_caps_malloc(
        sizeof(TrendPoint)*BuildingIdentifier::trend_slots,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
    if(!rows_mem || !trend_mem) {
        web.send(503,"application/json; charset=utf-8","{\"valid\":false}");
        return;
    }
    PlanRow* rows=rows_mem.get();          // trivially copyable PODs
    TrendPoint* trend=trend_mem.get();
    xSemaphoreTake(lock_data,portMAX_DELAY);
    const CalibrationReport cal=calibration_snapshot;
    const MpcPlanRecord plan=mpc_plan;
    const size_t row_count=mpc_rows?mpc_row_count:0;
    if(mpc_rows) std::copy(mpc_rows,mpc_rows+row_count,rows);
    const size_t trend_count=mpc_trend?mpc_trend_count:0;
    if(mpc_trend) std::copy(mpc_trend,mpc_trend+trend_count,trend);
    const bool energy=mpc_energy_in_kwh;
    const double energy_kwh=mpc_energy_kwh,cost=mpc_cost,uncertainty=mpc_uncertainty_c;
    const size_t hp=horizon_price_slots,hw=horizon_weather_slots,hs=horizon_slots;
    const HorizonLimit hl=horizon_limit;
    const BridgeStatus bs=bridge_status;
    const uint32_t solve_ms=mpc_solve_ms,worst=mpc_solve_worst_ms,solves=mpc_solves,
                   failures=mpc_solver_failures,saves=identifier_nvs_saves,
                   save_failures=identifier_nvs_failures,save_skips=identifier_nvs_skipped;
    const bool restored=identifier_restored;
    const int reset_state=identifier_reset_state.load();
    const uint32_t reset_failures=identifier_reset_failures.load();
    const ShellyTelemetry meter=consumption;
    const CN105Telemetry cp=pump;
    xSemaphoreGive(lock_data);
    const bool pump_verified=valid_cn105(cp,now) && cp.evidence.source_time_verified;
    const auto market=enevina_firmware::market_status();
    const int64_t current_slot=(now/900)*900;
    // Never display a plan from an earlier slot or from an uncalibrated model.
    const bool plan_ok=cal.state==CalibrationState::Calibrated && plan.has_plan &&
        validity==MpcFallback::None && arb_in.plan.computed_utc==plan.computed_utc &&
        plan.first_utc==current_slot && row_count>0;
    const bool energy_ok=plan_ok && energy;
    const bool confirmed=plan_ok && arb.source==ProposalSource::Mpc &&
        mpc_state_read_back(arb_in);
    const bool midea=cottage_midea_selected();
    const ModelEstimate& m=cal.model;
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8","");
    char buf[640];
    snprintf(buf,sizeof(buf),
        "{\"evidence\":\"MODEL_FROM_LOCAL_MEASUREMENTS\",\"now_utc\":%lld,\"state\":\"%s\","
        "\"fallback\":\"%s\",\"source\":\"%s\",\"armed\":%s,\"decision_confirmed\":%s,"
        "\"pump_source_verified\":%s,\"pump_connection_ready\":%s,"
        "\"calibration\":{\"state\":\"%s\",\"block\":\"%s\",\"samples\":%u,\"heating_slots\":%u,"
        "\"off_slots\":%u,\"starts\":%u,\"stops\":%u,\"skipped\":%u,\"outliers\":%u,"
        "\"windows\":%u,\"windows_with_switch\":%u,\"windows_needed\":%u,\"restored\":%s,"
        "\"nvs_saves\":%u,\"nvs_failures\":%u,\"nvs_skipped_low_space\":%u,"
        "\"reset_state\":\"%s\",\"reset_failures\":%u,",
        static_cast<long long>(now),mpc_run_state_name(arb.mpc_state),
        mpc_fallback_name(arb.fallback),proposal_source_name(arb.source),
        active_control_armed(midea)?"true":"false",
        confirmed?"true":"false",pump_verified?"true":"false",
        arb_in.pump_state_known?"true":"false",calibration_state_name(cal.state),calibration_block_name(cal.block),cal.samples,
        cal.heating_slots,cal.off_slots,cal.starts,cal.stops,cal.skipped,cal.outliers,
        cal.windows,cal.windows_with_switch,CalibrationLimits::windows_needed,
        restored?"true":"false",saves,save_failures,save_skips,
        reset_state==1?"REQUESTED":reset_state==2?"PERSISTED":
        reset_state==3?"PERSIST_FAILED":"NONE",reset_failures);
    web.sendContent(buf);
    const bool have_model=cal.samples>0 && m.tau_h>0;
    char a[24],b[24],c[24],d[24],e[24],f[24],g[24],h[24];
    json_number_or_null(a,sizeof(a),cal.validation_rms_c,"%.4f",cal.windows>0);
    json_number_or_null(b,sizeof(b),cal.validation_bias_c,"%.4f",cal.windows>0);
    json_number_or_null(c,sizeof(c),cal.validation_max_abs_c,"%.4f",cal.windows>0);
    json_number_or_null(d,sizeof(d),m.tau_h,"%.3f",have_model);
    json_number_or_null(e,sizeof(e),m.tau_std_h,"%.3f",have_model);
    json_number_or_null(f,sizeof(f),m.lift_k_ref,"%.3f",have_model);
    json_number_or_null(g,sizeof(g),m.lift_std_k,"%.3f",have_model);
    json_number_or_null(h,sizeof(h),m.lift_ref_outdoor_c,"%.2f",have_model);
    snprintf(buf,sizeof(buf),
        "\"validation_rms_c\":%s,\"validation_bias_c\":%s,\"validation_max_abs_c\":%s,"
        "\"planning_uncertainty_c\":%.3f,\"last_sample_utc\":%lld,"
        "\"model\":{\"plausible\":%s,\"tau_h\":%s,\"tau_std_h\":%s,\"lift_k\":%s,"
        "\"lift_std_k\":%s,\"lift_outdoor_c\":%s,",a,b,c,
        plan_ok?uncertainty:cal.planning_uncertainty_c,static_cast<long long>(cal.last_sample_utc),
        m.plausible?"true":"false",d,e,f,g,h);
    web.sendContent(buf);
    json_number_or_null(a,sizeof(a),m.lift_k_per_c,"%.4f",have_model);
    json_number_or_null(b,sizeof(b),m.start_delay_min,"%.2f",have_model);
    json_number_or_null(c,sizeof(c),m.start_delay_std_min,"%.2f",have_model);
    json_number_or_null(d,sizeof(d),m.afterheat_k,"%.4f",have_model);
    json_number_or_null(e,sizeof(e),m.one_step_sigma_c,"%.4f",have_model);
    json_number_or_null(f,sizeof(f),m.outdoor_min_c,"%.2f",have_model);
    json_number_or_null(g,sizeof(g),m.outdoor_max_c,"%.2f",have_model);
    json_number_or_null(h,sizeof(h),m.gain_rise_k,"%.3f",have_model);
    snprintf(buf,sizeof(buf),
        "\"lift_k_per_c\":%s,\"start_delay_min\":%s,\"start_delay_std_min\":%s,"
        "\"afterheat_k\":%s,\"one_step_sigma_c\":%s,\"outdoor_min_c\":%s,"
        "\"outdoor_max_c\":%s,\"gain_rise_k\":%s,\"cop_identified\":false,",
        a,b,c,d,e,f,g,h);
    web.sendContent(buf);
    json_number_or_null(a,sizeof(a),m.electric_on_kw_at_0c,"%.4f",m.electric_trusted);
    json_number_or_null(b,sizeof(b),m.electric_on_kw_per_c,"%.5f",m.electric_trusted);
    json_number_or_null(c,sizeof(c),m.electric_standby_kw,"%.4f",m.electric_trusted);
    json_number_or_null(d,sizeof(d),m.electric_start_kwh,"%.4f",m.electric_trusted);
    json_number_or_null(e,sizeof(e),m.ua_lower_kw_per_c,"%.4f",m.absolute_bounds);
    json_number_or_null(f,sizeof(f),m.capacity_lower_kwh_per_c,"%.3f",m.absolute_bounds);
    char w[24];
    json_number_or_null(w,sizeof(w),meter.watts,"%.1f",valid_shelly(meter,now));
    snprintf(buf,sizeof(buf),
        "\"electric_trusted\":%s,\"electric_on_kw_at_0c\":%s,\"electric_on_kw_per_c\":%s,"
        "\"electric_standby_kw\":%s,\"electric_start_kwh\":%s,\"ua_lower_kw_per_c\":%s,"
        "\"capacity_lower_kwh_per_c\":%s}},"
        "\"meter\":{\"scope\":\"%s\",\"watts\":%s,\"used_for_model\":%s},",
        m.electric_trusted?"true":"false",a,b,c,d,e,f,
        shelly_meter_pump_only()?"HEAT_PUMP_ONLY":"UNCONFIRMED",w,
        shelly_meter_pump_only()&&m.electric_trusted?"true":"false");
    web.sendContent(buf);
    json_number_or_null(a,sizeof(a),energy_kwh,"%.4f",energy_ok);
    json_number_or_null(b,sizeof(b),cost,"%.4f",energy_ok);
    snprintf(buf,sizeof(buf),
        "\"plan\":{\"valid\":%s,\"had_plan\":%s,\"computed_utc\":%lld,\"first_utc\":%lld,\"first_heat\":%s,"
        "\"planning_fallback\":\"%s\",\"plan_block\":\"%s\",\"bridge_status\":\"%s\",\"horizon_slots\":%u,"
        "\"horizon_limit\":\"%s\",\"price_slots\":%u,\"weather_slots\":%u,"
        "\"target_c\":%.2f,\"lower_c\":%.2f,\"upper_c\":%.2f,\"energy_valid\":%s,"
        "\"energy_kwh\":%s,\"cost\":%s,\"currency\":\"%s\",\"solve_ms\":%u,"
        "\"solve_worst_ms\":%u,\"solves\":%u,\"solver_failures\":%u,\"slots\":[",
        plan_ok?"true":"false",plan.has_plan?"true":"false",
        static_cast<long long>(plan.computed_utc),
        static_cast<long long>(plan.first_utc),plan_ok&&plan.first_heat?"true":"false",
        mpc_fallback_name(plan.planning_fallback),mpc_fallback_name(validity),
        bridge_status_name(bs),
        static_cast<unsigned>(hs),horizon_limit_name(hl),static_cast<unsigned>(hp),
        static_cast<unsigned>(hw),plan.target_c,plan.band.lower_c,plan.band.upper_c,
        energy_ok?"true":"false",a,b,market.currency,solve_ms,worst,solves,failures);
    web.sendContent(buf);
    for(size_t i=0;plan_ok && i<row_count;++i) {
        const PlanRow& r=rows[i];
        json_number_or_null(a,sizeof(a),r.energy_kwh,"%.5f",energy_ok);
        json_number_or_null(b,sizeof(b),r.cost,"%.6f",energy_ok);
        snprintf(buf,sizeof(buf),
            "%s{\"utc\":%lld,\"heat\":%s,\"setpoint_c\":%.2f,\"indoor_c\":%.3f,\"outdoor_c\":%.2f,"
            "\"price\":%.6f,\"kwh\":%s,\"cost\":%s}",i?",":"",
            static_cast<long long>(r.utc),r.heat?"true":"false",r.setpoint_c,r.indoor_c,r.outdoor_c,
            r.price,a,b);
        web.sendContent(buf);
        if(i%24==23) delay(1);
    }
    web.sendContent("]},\"trend\":[");
    for(size_t i=0;i<trend_count;++i) {
        const TrendPoint& t=trend[i];
        json_number_or_null(a,sizeof(a),t.one_step_c,"%.3f");
        json_number_or_null(b,sizeof(b),t.open_loop_c,"%.3f");
        snprintf(buf,sizeof(buf),"%s{\"utc\":%lld,\"measured_c\":%.3f,\"one_step_c\":%s,"
            "\"open_loop_c\":%s}",i?",":"",static_cast<long long>(t.utc),
            static_cast<double>(t.measured_c),a,b);
        web.sendContent(buf);
        if(i%24==23) delay(1);
    }
    web.sendContent("]}");
    web.sendContent("");
}
// Read-only hourly Finnish Meteorological Institute temperature forecast.
// Uses the same mutex-protected, validated cache as the optimizer; does not
// issue a new HTTPS request, infer missing data or alter heating controls.
static void weather_forecast_endpoint() {
    const auto weather=enevina_firmware::weather_status();
    constexpr size_t max_forecast_points=64;
    constexpr size_t max_emitted_points=48;
    auto* points=static_cast<enevina::OutdoorPoint*>(
        heap_caps_malloc(sizeof(enevina::OutdoorPoint)*max_forecast_points,
                         MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!points) {
        web.sendHeader("Cache-Control","no-store");
        web.send(503,"application/json; charset=utf-8",
                 "{\"valid\":false,\"slots\":[]}");
        return;
    }
    const int64_t now=time(nullptr);
    const size_t count=weather.available?
        enevina_firmware::future_weather(points,max_forecast_points):0;
    size_t available=0;
    for(size_t i=0;i<count;++i) {
        const auto& point=points[i];
        if(point.timestamp_utc>=now && point.timestamp_utc<=now+48*3600 &&
           std::isfinite(point.temperature_c) && point.temperature_c>=-60 &&
           point.temperature_c<=60 && ++available>=max_emitted_points)break;
    }
    web.sendHeader("Cache-Control","no-store");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200,"application/json; charset=utf-8","");
    char line[180];
    snprintf(line,sizeof(line),
        "{\"valid\":%s,\"source\":\"MET Norway Locationforecast 2.0\",\"fetched_utc\":%lld,\"slots\":[",
        available?"true":"false",static_cast<long long>(available?weather.fetched_utc:0));
    web.sendContent(line);
    size_t emitted=0;
    for(size_t i=0;i<count && emitted<max_emitted_points;++i) {
        const auto& point=points[i];
        if(point.timestamp_utc<now || point.timestamp_utc>now+48*3600 ||
           !std::isfinite(point.temperature_c) ||
           point.temperature_c < -60 || point.temperature_c > 60)
            continue;
        snprintf(line,sizeof(line),
             "%s{\"utc\":%lld,\"temperature_c\":%.3f}",
             emitted++?",":"",static_cast<long long>(point.timestamp_utc),
             point.temperature_c);
        web.sendContent(line);
        if(i%12==11) delay(1);
    }
    web.sendContent("]}");
    web.sendContent("");
    heap_caps_free(points);
}
// C2.5: Small, read-only 15-minute price feed for the local premium dashboard.
// Do not manufacture price points when the clock or market data is stale.
// Plan timeline: price classes over the published horizon with the SAME
// classification and profile call as the live controller. No predicted indoor
// temperature or cost is produced (no calibrated building model exists).
// Gathers one history sample from live state: only fresh, validated values;
// everything else stays missing. The outdoor value is the measured outdoor
// sensor when there is one, otherwise the weather FORECAST, flagged as such.
static enevina::cottage::HistoryInputs history_collect() {
    using enevina::cottage::HistoryInputs;
    HistoryInputs in;
    const int64_t now=time(nullptr);
    in.clock_trusted=clock_ok();in.utc=now;
    in.wifi_ok=wifi_ok.load();
    CN105Telemetry cp,observed;ShellyTelemetry st;
    int64_t climate_time=0;
    xSemaphoreTake(lock_data,portMAX_DELAY);
    cp=pump;observed=sse_observed_pump;st=consumption;climate_time=climate_utc;
    xSemaphoreGive(lock_data);
    const auto room=site_room_temperature_config();
    in.indoor_from_pump=room.source==RoomTemperatureSource::Pump;
    in.indoor_correction_c100=room.correction_c100;
    const bool midea=cottage_midea_selected();
    in.indoor_source_id=in.indoor_from_pump?
        (static_cast<uint64_t>(site_settings_endpoint_generation())<<32) |
            pump_configuration_revision(midea):
        c23_indoor_binding_id();
    const TemperatureSample te=selected_room_sample(now,room);
    in.indoor_valid=valid_temperature(te,now);
    in.indoor_c=te.indoor_c;
    in.sensor_online=in.indoor_valid;
    double rh=0,outdoor=0;bool rh_ok=false,outdoor_ok=false;
    c23_history_air(rh,rh_ok,outdoor,outdoor_ok);
    in.rh_valid=rh_ok;in.rh_pct=rh;
    if(outdoor_ok) {in.outdoor_valid=true;in.outdoor_c=outdoor;in.outdoor_is_forecast=false;}
    else {
        const auto ws=enevina_firmware::weather_status();
        if(ws.available && ws.next_utc>0 && ws.next_utc-now<7200 && now-ws.next_utc<7200) {
            in.outdoor_valid=true;in.outdoor_c=ws.next_temperature_c;in.outdoor_is_forecast=true;
        }
    }
    const bool sse_fresh=observed.received && climate_time>0 && now>=climate_time && now-climate_time<=180;
    if(uses_common_pump_state()) {
        const auto live=optimizer_pump_state(now,midea);
        in.pump_online=live.source==enevina::PumpStateSource::DirectLan ||
            live.source==enevina::PumpStateSource::VerifiedBus;
        in.setpoint_valid=in.pump_online && std::isfinite(live.setpoint_c);
        in.setpoint_c=live.setpoint_c;
    } else {
        in.pump_online=valid_cn105(cp,now) || sse_fresh;
        in.setpoint_valid=valid_cn105(cp,now);in.setpoint_c=cp.setpoint_c;
    }
    in.power_valid=valid_shelly(st,now);in.power_w=st.watts;
    in.power_sample_utc=st.captured_utc;
    in.power_scope=static_cast<uint8_t>(site_settings_snapshot().shelly_scope);
    const auto market=enevina_firmware::market_status();
    in.prices_ok=market.current_price_valid;
    double total=0;
    if(market.current_price_valid && site_slot_total_price((now/900)*900,market.current_spot_eur_per_kwh,total)) {
        in.price_valid=true;in.price_per_kwh=total;
        in.price_currency=enevina::cottage::history_currency_id(market.currency);
    }
    // History records the same *proposal* selected by the live MPC/price/
    // thermostat arbiter. It never claims that the pump executed the action;
    // separately counted confirmed commands carry that evidence.
    const BootstrapDecision d=active_control_proposal(now,te);
    in.action=d.action==BootstrapAction::Off?1:d.action==BootstrapAction::HeatSetpoint?2:
              d.action==BootstrapAction::Hold?3:0;
    in.armed=active_control_armed(midea);
    in.commands_sent=control_commands_sent.load();
    in.commands_confirmed=control_commands_confirmed.load();
    in.commands_failed=control_commands_failed.load();
    in.reset_reason=boot_reset_reason_code;
    in.ota_good_version=cottage_ota_good_version();
    in.ota_upgrade_event_version=cottage_ota_recorded_upgrade_version();
    return in;
}
// GET /api/freshness: which data the device is using, how old it is, when it
// expires and what the controller does if it is missing (same rules as the
// controller, see cottage/data_freshness.hpp).
static void freshness_endpoint() {
    using namespace enevina::cottage;
    FreshnessInputs in;
    const int64_t now=time(nullptr);
    in.clock_trusted=clock_ok();in.now=now;in.wifi_ok=wifi_ok.load();
    const auto market=enevina_firmware::market_status();
    const SiteSettings site=site_settings_snapshot();
    const char* area=nullptr;const char* currency=nullptr;int offset=0;
    in.price_zone_configured=site_market_region(site,area,currency,offset);
    in.price_current_valid=market.current_price_valid;
    // Reuse the live controller's validated total-price path, not spot-only
    // metadata. This checks the confirmed tariff, contiguous 8 slots and
    // actual proposal mode; never claim price control if bootstrap falls back.
    const TemperatureSample te=selected_room_sample(now);
    const BootstrapDecision actual=bootstrap_preview(now,te);
    in.price_decision_valid=actual.price_data_valid;
    in.no_heating_requested=actual.action==BootstrapAction::Off &&
        actual.reason==BootstrapReason::NoHeatingRequested;
    in.price_last_fetch=market.last_fetch_utc;in.price_valid_until=market.valid_until_utc;
    in.price_error=market.last_error;
    const auto ws=enevina_firmware::weather_status();
    in.weather_configured=site.fmi_latlon[0]!=0;in.weather_available=ws.available;
    in.weather_fetched=ws.fetched_utc;in.weather_valid_until=ws.valid_until_utc;
    CN105Telemetry cp,observed;
    int64_t climate_time=0;
    xSemaphoreTake(lock_data,portMAX_DELAY);
    cp=pump;observed=sse_observed_pump;climate_time=climate_utc;
    xSemaphoreGive(lock_data);
    in.sensor_bound=site_room_temperature_config().source==RoomTemperatureSource::Pump?
        site.pump_adapter[0]!=0:c23_indoor_bound();
    in.sensor_fresh=valid_temperature(te,now);
    in.sensor_age_s=te.received&&now>=te.captured_utc?now-te.captured_utc:-1;
    const bool sse_fresh=observed.received && climate_time>0 && now>=climate_time && now-climate_time<=180;
    in.pump_configured=site.pump_adapter[0]!=0;
    const bool midea=cottage_midea_selected();
    if(uses_common_pump_state()) {
        const auto live=optimizer_pump_state(now,midea);
        in.pump_fresh=live.source==enevina::PumpStateSource::DirectLan ||
            live.source==enevina::PumpStateSource::VerifiedBus;
        in.pump_age_s=in.pump_fresh?now-live.evidence.received_utc:-1;
    } else {
        const bool cn105_fresh=valid_cn105(cp,now);
        in.pump_fresh=cn105_fresh || sse_fresh;
        in.pump_age_s=verified_pump_age(now,cn105_fresh,cp.captured_utc,
                                        sse_fresh,climate_time);
    }
    in.control_armed=active_control_armed(midea);
    const Freshness f=assess_freshness(in);
    web.sendHeader("Cache-Control","no-store");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200,"application/json; charset=utf-8","");
    char chunk[512];
    size_t used=0;
    freshness_json(f,in.control_armed,
        control_gate_name(static_cast<ControlBlock>(control_gate.load())),
        [&](const char* part) {
        const size_t len=strlen(part);
        if(used+len>=sizeof(chunk)) {chunk[used]=0;web.sendContent(chunk);used=0;}
        memcpy(chunk+used,part,len);used+=len;
    });
    if(used) {chunk[used]=0;web.sendContent(chunk);}
    web.sendContent("");
}
static void history_worker(void*) {
    for(;;) {
        enevina_firmware::history_observe(history_collect());
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
static void timeline_endpoint() {
    const auto market=enevina_firmware::market_status();
    web.sendHeader("Cache-Control","no-store");
    if(!market.clock_trusted || !market.current_price_valid) {
        web.send(200,"application/json; charset=utf-8","{\"valid\":false,\"slots\":[],\"segments\":[]}");
        return;
    }
    constexpr size_t kMax=kMaxTimelineSlots;
    auto* points=static_cast<enevina_firmware::PricePoint*>(heap_caps_malloc(
        sizeof(enevina_firmware::PricePoint)*kMax,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto* utc=static_cast<int64_t*>(heap_caps_malloc(sizeof(int64_t)*kMax,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto* total=static_cast<double*>(heap_caps_malloc(sizeof(double)*kMax,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto* timeline=static_cast<Timeline*>(heap_caps_malloc(sizeof(Timeline),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto release=[&]() {
        heap_caps_free(points);heap_caps_free(utc);heap_caps_free(total);heap_caps_free(timeline);
    };
    if(!points||!utc||!total||!timeline) {
        release();
        web.send(503,"application/json; charset=utf-8","{\"valid\":false,\"slots\":[],\"segments\":[]}");
        return;
    }
    const size_t have=enevina_firmware::future_prices(points,kMax);
    size_t n=0;
    for(size_t i=0;i<have;++i) {
        double t=0;
        if(!std::isfinite(points[i].spot_eur_per_kwh) ||
           !site_slot_total_price(points[i].start_utc,points[i].spot_eur_per_kwh,t)) break;
        utc[n]=points[i].start_utc;total[n]=t;++n;
    }
    const SiteSettings site=site_settings_snapshot();
    const int64_t now=time(nullptr);
    const CottageModePlan plan=c23_current_mode_plan();
    const CottageModeConfig mode=c23_mode_config();
    const bool has_target=mode.mode!=CottageMode::Cold && plan.has_target && std::isfinite(plan.target_c);
    TimelineParams params;TimelineMeta meta;
    const bool setup=timeline_setup(site.profile,has_target?plan.target_c:-1,site.comfort_min_c,
        site.comfort_max_c,site.min_price_advantage_fraction,params,meta);
    params.price_limit_scale=currency_limit_scale(market.currency);
    params.advantage_eur_floor*=params.price_limit_scale;
    if(!setup || !build_timeline(utc,total,n,params,*timeline)) {
        release();
        web.send(200,"application/json; charset=utf-8","{\"valid\":false,\"slots\":[],\"segments\":[]}");
        return;
    }
    const TemperatureSample indoor=selected_room_sample(now);
    ArbiterOutput active{};
    const BootstrapDecision decision=active_control_proposal(now,indoor,&active);
    // The displayed "now" proposal must be identical to what the controller
    // arbitrates (MPC first, then price, then thermostat), never a standalone
    // price fallback when an MPC plan is active.
    // The timeline is advice, not a bypass around the live controller's
    // independent room-sensor freshness and Drying humidity gates.
    meta.indoor_fresh=valid_temperature(indoor,now);
    meta.heating_permitted=has_target && meta.indoor_fresh &&
        !(mode.mode==CottageMode::Drying && drying_blocks_heat(plan));
    meta.currency=market.currency;meta.area=market.area;
    meta.decision_action=bootstrap_action_name(decision.action);
    meta.decision_source=proposal_source_name(active.source);
    meta.decision_reason=bootstrap_reason_name(decision.reason);
    // MPC sets price_data_valid internally to make a proposal executable in
    // its own branch; the wire's price_valid means verified spot fallback.
    meta.decision_price_valid=bootstrap_preview(now,indoor).price_data_valid;
    meta.decision_setpoint_c=decision.action==BootstrapAction::HeatSetpoint?decision.heat_setpoint_c:-1;
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200,"application/json; charset=utf-8","");
    char chunk[768];
    size_t used=0;
    timeline_json(meta,*timeline,[&](const char* part) {
        const size_t len=strlen(part);
        if(used+len>=sizeof(chunk)) {chunk[used]=0;web.sendContent(chunk);used=0;delay(1);}
        if(len>=sizeof(chunk)) {web.sendContent(part);return;}
        memcpy(chunk+used,part,len);used+=len;
    });
    if(used) {chunk[used]=0;web.sendContent(chunk);}
    web.sendContent("");
    release();
}
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
        double total=0;
        const bool has_total=site_slot_total_price(price.start_utc,
            price.spot_eur_per_kwh,total) && std::isfinite(total);
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
    site_settings_initialize(); // NVS site settings before any reader starts
    cottage_midea_service_initialize(); // separate Midea V3 NVS, no CN105 changes
    c23_initialize(); // NVS mode + persistent explicit Ruuvi bonding
    cottage_ota_initialize(); // Persist successful signed release + reject failed candidate
    // Phase 1 commissioning: stored home Wi-Fi (NVS, else the compile-time
    // default) or the per-device setup AP. No pump/device contact here.
    cottage_wifi_begin(web);
    setenv("TZ","EET-2EEST,M3.5.0/3,M10.5.0/4",1);
    tzset();
    configTime(0,0,"pool.ntp.org","time.google.com");
    web.on("/",HTTP_GET,[](){
        web.sendHeader("Cache-Control","no-store");
        if(cottage_wifi_request_via_ap(web)) {
            // Setup network: phase 1 shows ONLY the Wi-Fi page.
            web.send_P(200,"text/html; charset=utf-8",cottage_wifi_setup_html);
            return;
        }
        web.sendHeader("X-Content-Type-Options","nosniff");
        web.sendHeader("Content-Security-Policy",
            "default-src 'self'; connect-src 'self'; img-src 'self' data:; "
            "script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
            "frame-ancestors 'none'; base-uri 'none'");
        web.send_P(200,"text/html; charset=utf-8",premium_dashboard_html);
    });
    web.on("/api/status",HTTP_GET,[](){
        if(cottage_wifi_require_home_lan(web))status_endpoint();
    });
    web.on("/api/ota/install",HTTP_POST,[](){
        // Reuse the existing local settings authorisation and reject open
        // provisioning-AP requests. No new accounts, passwords or PINs.
        if(WiFi.status()!=WL_CONNECTED || cottage_wifi_request_via_ap(web) ||
           !web.hasArg("token") ||
           !c23_authorize_local_setting(web.arg("token").c_str())) {
            web.send(403,"application/json",
                     "{\"queued\":false,\"error\":\"TOKEN\"}");
            return;
        }
        if(!cottage_ota_configured() || cottage_ota_confirmation_pending() ||
           cottage_ota_network_maintenance() ||
           !cottage_ota_request_immediate()) {
            web.send(409,"application/json",
                     "{\"queued\":false,\"error\":\"NOT_READY\"}");
            return;
        }
        web.sendHeader("Cache-Control","no-store");
        web.send(202,"application/json","{\"queued\":true}");
    });
    web.on("/api/plan",HTTP_GET,[](){
        if(cottage_wifi_require_home_lan(web))plan_endpoint();
    });
    web.on("/api/freshness",HTTP_GET,[](){
        if(cottage_wifi_require_home_lan(web))freshness_endpoint();
    });
    web.on("/api/history",HTTP_GET,[](){
        if(cottage_wifi_require_home_lan(web))enevina_firmware::history_http(web);
    });
    web.on("/api/timeline",HTTP_GET,[](){
        if(cottage_wifi_require_home_lan(web))timeline_endpoint();
    });
    web.on("/api/mpc",HTTP_GET,[](){
        if(cottage_wifi_require_home_lan(web))mpc_endpoint();
    });
    // Restart building identification (e.g. after renovation). Same local
    // setting token as every other setting; no new authentication.
    web.on("/api/model/reset",HTTP_POST,[](){
        if(!cottage_wifi_require_home_lan(web))return;
        if(!web.hasArg("token") ||
           !c23_authorize_local_setting(web.arg("token").c_str())) {
            web.send(403,"application/json","{\"queued\":false}");
            return;
        }
        identifier_reset_state.store(1);
        identifier_reset_requested.store(true);
        // Queued, NOT done: whether the reset reached flash is reported by
        // /api/mpc (calibration.reset_state) once the optimizer task has verified it.
        web.send(202,"application/json","{\"queued\":true,\"persisted\":false}");
    });
    web.on("/api/prices",HTTP_GET,[](){
        if(cottage_wifi_require_home_lan(web))prices_endpoint();
    });
    web.on("/api/weather",HTTP_GET,weather_forecast_endpoint);
    c23_register_routes(web);
    site_settings_register_routes(web);
    cottage_midea_service_register_routes(web);
    cottage_gree_register_routes(web);
    cottage_daikin_register_routes(web);
    cottage_daikin_dsiot_register_routes(web);
    cottage_cn105_direct_register_routes(web);
    cottage_daikin_s21_register_routes(web);
    cottage_haier_register_routes(web);
    cottage_panasonic_register_routes(web);
    cottage_wf_rac_register_routes(web);
    cottage_control_registration(); // restore prior explicit choice only
    web.onNotFound([](){
        if(cottage_wifi_request_via_ap(web)) {
            // Captive portal: any unknown host/path on the setup AP -> /setup.
            web.sendHeader("Location",String("http://")+
                WiFi.softAPIP().toString()+"/setup");
            web.send(302,"text/plain","");
            return;
        }
        web.send(404,"text/plain","Ei löydy");
    });
    web.begin();
    boot_ui_started=true;
    boot_validation_started_ms=millis();
    enableLoopWDT(); // Arduino ESP32 loop-task hardware watchdog.
    // Initialize BLE before starting memory-heavy HTTPS workers.
    xTaskCreatePinnedToCore(c23_ble_worker,"cottage_ruuvi",6144,nullptr,1,&task_ruuvi,0);
    for(int i=0;i<150 && !c23_ble_initialized();++i)
        delay(20);
    if(!c23_ble_initialized())
        Serial.println("C24_WARN: BLE initialization not confirmed");
    enevina_firmware::history_initialize();   // PSRAM ring + dedicated history partition
    enevina_firmware::start_market_reader();
    enevina_firmware::start_weather_reader(site_settings_snapshot().fmi_latlon);
    xTaskCreatePinnedToCore(sse_worker,"cottage_cn105",8192,nullptr,2,&task_sse,0);
    // Lazily start the Midea polling task after a Midea is actually selected.
    // A CN105 controller must not reserve a second pump driver's stack.
    xTaskCreatePinnedToCore(shelly_worker,"cottage_shelly",5120,nullptr,2,&task_shelly,0);
    xTaskCreatePinnedToCore(history_worker,"cottage_hist",6144,nullptr,1,&task_history,0);
    // BLE already running. CN105 writes are gated by explicit local arming.
    xTaskCreatePinnedToCore(optimizer_worker,"cottage_optimizer",16384,nullptr,1,&task_optimizer,1);
    if(cottage_ota_configured() &&
        xTaskCreatePinnedToCore(cottage_ota_worker,"cottage_ota",
                                10240,nullptr,1,&task_ota,1)!=pdPASS) {
        task_ota=nullptr;
        Serial.println("C24_OTA_ERROR: signed OTA task creation failed");
    }
    Serial.println("ENEVINA LITE: CN105 controller, persisted local choice");
}
static void cottage_local_lan_worker(void*) {
    for(;;) {
        if(cottage_gree_needs_worker())cottage_gree_tick();
        if(cottage_daikin_needs_worker())cottage_daikin_tick();
        if(cottage_daikin_dsiot_needs_worker())cottage_daikin_dsiot_tick();
        if(cottage_wf_rac_needs_worker())cottage_wf_rac_tick();
        if(cottage_cn105_direct_needs_worker())cottage_cn105_direct_tick();
        if(cottage_daikin_s21_needs_worker())cottage_daikin_s21_tick();
        if(cottage_haier_needs_worker())cottage_haier_tick();
        if(cottage_panasonic_needs_worker())cottage_panasonic_tick();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
void loop() {
    wifi_ok.store(WiFi.status()==WL_CONNECTED);
    web_loop_heartbeat_ms.store(millis());
    web.handleClient();
    cottage_wifi_loop();
    // This is a once-per-selection task creation, not a polling/watchdog.
    // A saved Midea enrollment must also start after switching adapters in UI.
    static uint32_t last_midea_check_ms=0;
    if(static_cast<uint32_t>(millis()-last_midea_check_ms)>=2000) {
        last_midea_check_ms=millis();
        if(!task_midea && cottage_midea_service_needs_worker() &&
           xTaskCreatePinnedToCore(cottage_midea_service_worker,"cottage_midea",
                                   10240,nullptr,1,&task_midea,0)!=pdPASS)
            Serial.println("MIDEA_WORKER: insufficient memory");
        if(!task_local_lan && (cottage_gree_needs_worker() || cottage_daikin_needs_worker() ||
           cottage_daikin_dsiot_needs_worker() || cottage_wf_rac_needs_worker() ||
           cottage_cn105_direct_needs_worker() || cottage_daikin_s21_needs_worker() ||
           cottage_haier_needs_worker() || cottage_panasonic_needs_worker()) &&
           xTaskCreatePinnedToCore(cottage_local_lan_worker,"cottage_local_lan",8192,nullptr,1,&task_local_lan,0)!=pdPASS)
            Serial.println("LOCAL_LAN_WORKER: insufficient memory");
    }
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
