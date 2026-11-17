#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
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
#include "cottage/sse_transport.hpp"
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
static ObservedModeDuration observed_mode_duration{};
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
static TaskHandle_t task_sse=nullptr,task_shelly=nullptr;
static TaskHandle_t task_ruuvi=nullptr,task_optimizer=nullptr;
static TaskHandle_t task_ota=nullptr;
static bool shelly_relay_on=false;
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
       state!=ESP_OTA_IMG_PENDING_VERIFY) return;
    const uint32_t elapsed=millis()-boot_validation_started_ms;
#ifdef ENEVINA_OTA_TEST_RESET_BEFORE_CONFIRM
    // Explicitly gated physical regression test: simulate sudden app reset
    // while the new OTA image is pending. Only enable in a special signed
    // fault-test release, never in the field/production firmware.
    if(elapsed>=15000) {
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
        task_sse!=nullptr && task_shelly!=nullptr && task_optimizer!=nullptr;
    if(healthy) {
        if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK) {
            cottage_ota_confirm_boot();
            ota_boot_confirmed=true;
        }
        else Serial.println("C24_OTA_ERROR: boot validation could not commit");
    } else if(elapsed>120000) {
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
    cJSON* root=cJSON_Parse(raw);
    if(!root) return;
    const cJSON* key=cJSON_GetObjectItemCaseSensitive(root,"id");
    const int64_t now=clock_ok()?time(nullptr):0;
    if(!cJSON_IsString(key)||!now) {cJSON_Delete(root);return;}
    CN105Telemetry copy;
    xSemaphoreTake(lock_data,portMAX_DELAY);copy=pump;xSemaphoreGive(lock_data);
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
            observed_mode_duration.observe(copy.mode,
                static_cast<uint64_t>(esp_timer_get_time()/1000),continuous);
        }
        climate_utc=climate_stamp;compressor_utc=compressor_stamp;
        copy.captured_utc=climate_stamp&&compressor_stamp?
            std::min(climate_stamp,compressor_stamp):0;
        copy.received=(copy.captured_utc>0 &&
            now>=copy.captured_utc && now-copy.captured_utc<=180);
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
            xSemaphoreTake(lock_data,portMAX_DELAY);
            cp=pump;
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
                last_plan=result;last_optimized_utc=now;
                if(result.status==MpcStatus::Ok && elapsed<=2000) {
                    std::copy(rows,rows+max_horizon_slots,plan_rows.begin());
                    plan_count=result.count;
                } else {
                    plan_count=0;
                    last_plan.status=MpcStatus::ResourceLimit;
                }
                strlcpy(reason_text,result.status==MpcStatus::Ok && elapsed<=2000?
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
        // Recompute on quarter-hour boundary, retries every minute when offline.
        const time_t t=time(nullptr);
        int sleep_s=clock_ok()?static_cast<int>(900-(t%900)):60;
        if(sleep_s>60)sleep_s=60; // Allows freshness monitoring during long slots.
        vTaskDelay(pdMS_TO_TICKS(std::max(5,sleep_s)*1000));
    }
}
static const char html[] PROGMEM=R"HTML(<!doctype html><html lang="fi"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Enevina Lite – Pihamökki</title>
<style>body{font:16px system-ui;color:#193b33;background:#f6f8f6;margin:0;padding:18px}
main{max-width:980px;margin:auto}.notice{background:#fff0d9;padding:12px;border:2px solid #9d6b11}
section{background:white;padding:16px;border-radius:12px;margin:15px 0}
dl{display:grid;grid-template-columns:1fr auto;gap:10px}dt,dd{margin:0}
small{color:#64746d}table{border-collapse:collapse;width:100%}td,th{border-bottom:1px solid #ddd;padding:7px;text-align:left}
</style><main><h1>Enevina Lite – Pihamökki</h1>
<p class="notice"><strong>VARJOTILA – EI OHJAA LÄMPÖPUMPPUA</strong></p>
<section><h2>Tila ja mittaukset</h2><dl id="metrics"></dl><p id="reason"></p>
<small>Pumpun oma lämpötila ei korvaa riippumatonta jäätymissuoja-anturia.
CN105-yhteyden uptime ei tee vanhoista pumpun mittauksista tuoreita.
Synteettinen lämpömalli ei ole kalibrointi eikä säästötodiste.</small></section>
<section><h2>Laskettu aikataulu</h2><p id="result">Ei laskelmaa</p>
<table><thead><tr><th>Aika</th><th>Ehdotus</th><th>Ennuste °C</th><th>kWh</th><th>€</th></tr></thead>
<tbody id="rows"></tbody></table>
<small>Laskelmat synteettisellä mallilla eivät osoita todellisia säästöjä.</small></section>
<script>async function refresh(){try{
const s=await (await fetch('/api/status',{cache:'no-store'})).json();
const shown=[['CN105',s.cn105],['Pumppu',s.pump_mode],['Pumpun sisälämpö °C',s.pump_indoor_c],
['Riippumaton anturi °C',s.independent_c],['Kompressori Hz',s.compressor_hz],
['Shelly mökin kokonaisteho W',s.shelly_w],['Shellyn rele päällä',s.shelly_relay_on],['Spot €/kWh',s.spot],
['Sääennuste °C',s.weather_c],['Kokonaishinnan arvio €/kWh',s.total_price],
['MPC tila',s.mpc_status],['MPC aika ms',s.mpc_ms],['Pahin aika ms',s.mpc_worst_ms],
['Laskettu UTC',s.optimized_utc],['Vapaa RAM B',s.free_heap],
['Vapaa PSRAM B',s.free_psram],['Pinon vapaa alaraja B',s.stack_free_bytes],
['SSE-yhteys aktiivinen',s.sse_connected],['SSE HTTP',s.sse_http_status],
['SSE-yhdistämisiä',s.sse_connections],['SSE-katkoja',s.sse_disconnects],
['Pumpun lämpötilatapahtumia',s.sse_climate_events],
['Kompressoritaajuustapahtumia',s.sse_compressor_events],
['CN105-yhteyden heartbeat-lukuja',s.sse_link_events],
['Pumpun viimeisin lämpötapahtuma UTC',s.cn105_climate_utc?new Date(s.cn105_climate_utc*1000).toISOString():'Ei mittausta'],
['Kompressorin viimeisin tapahtuma UTC',s.cn105_compressor_utc?new Date(s.cn105_compressor_utc*1000).toISOString():'Ei mittausta'],
['CN105-yhteyden uptime s',s.cn105_link_uptime_s],
['Shelly lukuja OK/virhe',s.shelly_reads_ok+'/'+s.shelly_reads_bad],
['Shelly viimeisin vaste ms',s.shelly_rtt_ms],
['Shelly viimeisin UTC',s.shelly_last_read_utc?new Date(s.shelly_last_read_utc*1000).toISOString():'Ei mittausta'],
['Nord Pool jaksoja',s.market_slots],['Nord Pool päivitetty UTC',s.market_last_fetch_utc?new Date(s.market_last_fetch_utc*1000).toISOString():'Ei mittausta'],
['FMI pisteitä',s.weather_points],['FMI päivitetty UTC',s.weather_last_fetch_utc?new Date(s.weather_last_fetch_utc*1000).toISOString():'Ei mittausta'],
['Supervisor tila/syy',s.safety_mode+'/'+s.safety_reason],
['Lämpömalli kalibroitu',s.model_calibrated],
['Riippumaton pakkassuoja',s.independent_freeze_protection_verified]];
document.querySelector('#metrics').replaceChildren(...shown.flatMap(([k,v])=>{
let dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=k;dd.textContent=v??'Ei mittausta';return[dt,dd]}));
document.querySelector('#reason').textContent=s.reason;
const p=await (await fetch('/api/plan',{cache:'no-store'})).json();
document.querySelector('#result').textContent=p.valid?
'Kustannus '+p.cost_eur.toFixed(3)+' €; min '+p.minimum_c.toFixed(2)+' °C; '+p.count+' varttia':
'Ei hyväksyttyä aikataulua';
document.querySelector('#rows').replaceChildren(...(p.slots||[]).slice(0,192).map(x=>{
let tr=document.createElement('tr');
for(let v of [new Date(x.utc*1000).toLocaleString('fi-FI',{timeZone:'Europe/Helsinki'}),
x.heat?'HEAT 16 °C':'OFF',x.indoor_c.toFixed(2),x.kwh.toFixed(3),x.eur.toFixed(3)])
{let td=document.createElement('td');td.textContent=v;tr.append(td)}return tr}));
}catch(e){document.querySelector('#reason').textContent='Selain ei saa mittauksia'}}
refresh();setInterval(refresh,15000);</script>
<section id="air-panel">
<h2>Pihamökki – sisäilma ja kuivanapito</h2>
<p class="notice"><strong>VAIN VARJOTILA</strong> – tila tai tavoite ei ohjaa Mitsubishiä.
+16 °C on pumpun normaali alin lämmitysasetus. Rakenteen kosteus- tai
jäätymisturvallisuutta ei voi vahvistaa pelkällä huoneilman anturilla.</p>
<dl id="air-metrics"><dt>RuuviTag</dt><dd>Ei mittausta</dd></dl>
<h3>Toimintatila – vain laskenta</h3>
<form id="air-config">
<label for="air-mode">Toimintatila</label>
<select id="air-mode"><option value="0">Kylmillään</option>
<option value="1">Kuivanapito</option><option value="2">Ylläpitolämpö</option></select>
<label for="air-hold">Ylläpitolämmön tavoite °C</label>
<input id="air-hold" type="number" step="0.5" min="5" max="24" value="12">
<label for="air-delta">Kuivanapidon ulkolämpötilaero °C (kokeellinen)</label>
<input id="air-delta" type="number" step="0.5" min="3" max="5" value="4">
<button type="submit">Tallenna laskennallinen tila</button>
</form>
<p id="air-mode-status">Ei laskennallista ohjausta</p>
<h3>RuuviTagin tunnistus ja sitominen</h3>
<p>Anturin tunnistetta ei valita automaattisesti. Varmista fyysisesti,
että tunniste on juuri pihamökin anturista. Sitominen tallentuu
vain tähän laitteeseen; muut anturit hylätään.</p>
<form id="air-bond"><label for="air-role">Anturi</label>
<select id="air-role"><option value="indoor">Mökin sisäilma</option>
<option value="outdoor">Vapaaehtoinen ulkoilma</option></select>
<label for="air-mac">Ruuvi RAWv2 MAC (XX:XX:XX:XX:XX:XX) tai "none"</label>
<input id="air-mac" type="text" maxlength="17" autocomplete="off" placeholder="XX:XX:XX:XX:XX:XX">
<button type="submit">Sido anturi varjotilaan</button></form>
<p id="air-settings-status"></p>
<h3>Kosteuden seuranta ja hälytykset</h3>
<form id="air-thresholds">
<label for="air-rh70">Pitkän RH-seurannan raja % (6 tuntia)</label>
<input type="number" min="55" max="80" step="1" id="air-rh70" value="70">
<label for="air-rh80">Korkean RH:n tarkistushälytys % (24 tuntia)</label>
<input type="number" min="75" max="95" step="1" id="air-rh80" value="80">
<label for="air-battery">Paristojännitteen varoitusraja V</label>
<input type="number" min="2.0" max="3.2" step="0.05" id="air-battery" value="2.5">
<button type="submit">Tallenna varoitusrajat</button></form>
<ul id="air-alerts"><li>Ei mittausta</li></ul>
<small>70 % / 80 % RH ovat suuntaa-antavia pitkäaikaisseurannan rajoja,
eivät yleispäteviä rakenteen home- tai kondensaatiorajoja.
Lämmittäminen ei korvaa ilmanvaihtoa.</small>
<h3>Mittaus- ja tapahtumahistoria</h3>
<div style="overflow-x:auto"><table><thead><tr><th>Aika</th><th>°C</th>
<th>RH %</th><th>Kaste °C</th><th>g/m³</th></tr></thead>
<tbody id="air-history"></tbody></table></div>
<ul id="air-events"></ul>
</section>
<style>
#air-panel form{display:grid;gap:9px;max-width:430px;margin:12px 0 24px}
#air-panel input,#air-panel select,#air-panel button{
 font:inherit;min-height:42px;padding:7px;border-radius:7px;border:1px solid #7c948b}
#air-panel button{background:#e7f3ea;cursor:pointer}
#air-panel ul{padding-left:23px}
</style>
<script>
let c23token='',c23initialized=false,c23lastHistory=0;
const fmt2=v=>typeof v==='number' && Number.isFinite(v)?v.toFixed(2):'Ei mittausta';
const airNames=['Pitkä RH >70 %','RH >80 % pitkään plusasteissa',
'RuuviTag ei ole tuore','RuuviTag-paristo heikko','Sisäilma pakkasen puolella'];
const modes=['Kylmillään','Kuivanapito','Ylläpitolämpö'];
const notes=['Ei lämmitystä','Anturi puuttuu tai ei tuore','RH:n pitkäaikaisseurantaa',
'Mahdollinen kuivanapidon tarve','Laskennallinen lämmitystarve',
'Alle 16 °C ei toteudu pumpun suoralla asetuksella',
'Ulkolämpötila puuttuu; kuivanapidon tavoite ei kelpaa'];
async function airPost(url,fields){
 const body=new URLSearchParams({...fields,token:c23token});
 const result=await fetch(url,{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
 if(!result.ok)throw Error('Virhe '+result.status);
 return result.json();
}
document.getElementById('air-config').addEventListener('submit',async e=>{
 e.preventDefault();
 try{
 await airPost('/api/air/config',{
 mode:document.getElementById('air-mode').value,
 hold:document.getElementById('air-hold').value,
 delta:document.getElementById('air-delta').value});
 document.getElementById('air-settings-status').textContent='Laskennallinen tila tallennettu. Ei laiteohjausta.';
 }catch(err){document.getElementById('air-settings-status').textContent=err.message;}
});
document.getElementById('air-thresholds').addEventListener('submit',async e=>{
 e.preventDefault();
 try{
 await airPost('/api/air/thresholds',{
 rh70:document.getElementById('air-rh70').value,
 rh80:document.getElementById('air-rh80').value,
 battery:document.getElementById('air-battery').value});
 document.getElementById('air-settings-status').textContent='Seurantakynnykset tallennettu, ei ohjausta.';
 }catch(err){document.getElementById('air-settings-status').textContent=err.message;}
});
document.getElementById('air-bond').addEventListener('submit',async e=>{
 e.preventDefault();
 try{
 await airPost('/api/air/bind',{
 which:document.getElementById('air-role').value,
 mac:document.getElementById('air-mac').value.trim()});
 document.getElementById('air-settings-status').textContent='Anturisidonta tallennettu; odotetaan tuoretta mittausta.';
 }catch(err){document.getElementById('air-settings-status').textContent=err.message;}
});
async function airRefresh(){
 try{
 const s=await (await fetch('/api/status',{cache:'no-store'})).json();
 const a=s.air;if(!a)return;
 c23token=a.config_token||'';
 if(!c23initialized){
 document.getElementById('air-mode').value=String(a.mode);
 document.getElementById('air-hold').value=a.hold_target_c;
 document.getElementById('air-delta').value=a.drying_delta_c;
 document.getElementById('air-rh70').value=a.warning_rh70;
 document.getElementById('air-rh80').value=a.warning_rh80;
 document.getElementById('air-battery').value=a.warning_battery_v;
 c23initialized=true;
 }
 const rows=[
 ['Tila (laskennallinen)',modes[a.mode]||'Tuntematon'],
 ['Sidottu RuuviTag',a.bound_mac||'Ei sidottua anturia'],
 ['Läheltä löytynyt RuuviTag',a.discovered_mac||'Ei havaintoa'],
 ['Anturi tuore',a.sensor_fresh],
 ['Viimeisen mittauksen ikä s',fmt2(a.age_s)],
 ['Lämpötila °C',fmt2(a.temperature_c)],['RH %',fmt2(a.rh_pct)],
 ['Kastepiste °C',fmt2(a.dewpoint_c)],
 ['Absoluuttinen kosteus g/m³',fmt2(a.absolute_gm3)],
 ['Vesihöyryn osapaine Pa',fmt2(a.vapour_pressure_pa)],
 ['Ilmanpaine Pa',fmt2(a.pressure_pa)],
 ['Paristojännite V',fmt2(a.battery_v)],
 ['BLE-signaali dBm',fmt2(a.rssi_dbm)],
 ['Ulkolämpötila °C',fmt2(a.outside_temperature_c)],
 ['Ulkolämpötila lähde',a.outside_temp_source==='FMI_FORECAST'?'FMI:n ennuste':
 a.outside_temp_source==='RUUVI_RAWV2'?'Erillinen RuuviTag':'Puuttuu'],
 ['Ulkoanturi RH %',fmt2(a.outside_rh_pct)],
 ['Sisä–ulko °C',fmt2(a.inside_outside_c)],
 ['Sisä–ulko kosteus g/m³',fmt2(a.drying_potential_gm3)],
 ['Mahdollinen tuuletuksen kuivauspotentiaali',a.outside_sensor_fresh?
   (a.possible_ventilation_drying?'Suuntaa-antavasti kyllä':'Ei osoitettua potentiaalia'):'Ulko-RH puuttuu'],
 ['RH:n 6 h muutos, prosenttiyksikköä',fmt2(a.rh_6h_change_pct)],
 ['Absoluuttisen kosteuden 6 h muutos g/m³',fmt2(a.absolute_6h_change_gm3)],
 ['RH yli 70 % yhtäjaksoisesti h',fmt2(a.rh70_duration_h)],
 ['RH yli 80 % yhtäjaksoisesti h',fmt2(a.rh80_duration_h)],
 ['Hyväksytyt/duplikaatit/hylätyt',a.sample_count+'/'+a.duplicate_count+'/'+a.rejected_count],
 ['Tuntihistorian pisteet',a.history_hours],['NVS-tallennukset',a.nvs_checkpoints],
 ['Alhaisen heapin takia väliin jääneet BLE-skannaukset',a.low_heap_scan_skips]];
 document.getElementById('air-metrics').replaceChildren(...rows.flatMap(([key,val])=>{
 let dt=document.createElement('dt'),dd=document.createElement('dd');
 dt.textContent=key;dd.textContent=val===null||val===undefined?'Ei mittausta':String(val);
 return [dt,dd]}));
 const target=a.target_available?fmt2(a.target_c)+' °C':'Ei kelvollista tavoitetta';
 document.getElementById('air-mode-status').textContent=
 'Laskettu tavoite: '+target+'. '+(notes[a.advisory]||'')+
 '. Lämmitystarvearvio: '+(a.advisory_heat_needed?'kyllä':'ei')+
 '. Todellinen pumpun tila: '+(s.pump_mode||'ei tietoa')+
 '. '+(a.target_below_16?'Tavoite alle +16 °C ei ole suora Mitsubishi-asetus. ':'')+
 'Vain varjotila; fyysisiä ohjauksia ei ole.';
 const alerts=document.getElementById('air-alerts');
 alerts.replaceChildren();
 for(const item of a.alerts||[]){
 if(!item.active)continue;
 const li=document.createElement('li');
 li.textContent=(airNames[item.kind]||'Hälytys')+(item.ack?' (kuitattu)':' (aktiivinen) ');
 if(!item.ack){
 const button=document.createElement('button');button.textContent='Kuittaa';
 button.addEventListener('click',async()=>{
 try{await airPost('/api/air/ack',{alert:String(item.kind)});airRefresh();}
 catch(err){document.getElementById('air-settings-status').textContent=err.message;}});
 li.appendChild(button);}
 alerts.appendChild(li);
 }
 if(!alerts.children.length){const li=document.createElement('li');
 li.textContent='Ei aktiivisia hälytyksiä; tämä ei todista rakenteen turvallisuutta.';
 alerts.appendChild(li);}
 const log=document.getElementById('air-events');
 log.replaceChildren(...(a.events||[]).slice(0,8).map(item=>{
 const li=document.createElement('li');
 li.textContent=(item.ts_utc?new Date(item.ts_utc*1000).toLocaleString('fi-FI'):'Aika ei synkronoitu')+
 ' · tapahtumatyyppi '+item.kind+' / '+item.code;
 return li;}));
 if(Date.now()-c23lastHistory>60000){
 c23lastHistory=Date.now();
 const h=await(await fetch('/api/air/history',{cache:'no-store'})).json();
 const tbody=document.getElementById('air-history');tbody.replaceChildren();
 for(const entry of (h.hours||[]).slice(-48)){
 const tr=document.createElement('tr');
 const hour=entry.clock_valid?
 new Date(entry.hour*3600000).toLocaleString('fi-FI'):
 'Käynnistyksen tunti '+(entry.hour-2147483648);
 for(const value of [hour,fmt2(entry.temp),fmt2(entry.rh),fmt2(entry.dew),fmt2(entry.abs)]){
 const td=document.createElement('td');td.textContent=value;tr.appendChild(td);}
 tbody.appendChild(tr);}
 }
 }catch(err){
 document.getElementById('air-settings-status').textContent='Sisäilman tilaa ei saada: '+err.message;
 }
}
airRefresh();setInterval(airRefresh,15000);
</script></main></html>)HTML";
static void status_endpoint() {
    CN105Telemetry cp;ShellyTelemetry st;TemperatureSample te;
    BridgeStatus bs;MpcResult plan;Decision current_safety;
    int64_t optimized=0,climate_time=0,compressor_time=0,link_time=0;
    double link_uptime_s=0;
    bool relay_on=false;
    char reason[80];
    uint32_t ms,worst,heap,psram,stack;
    xSemaphoreTake(lock_data,portMAX_DELAY);
    cp=pump;st=consumption;/* C2.3 temperature read outside lock */bs=bridge_status;
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
    cJSON* root=cJSON_CreateObject();
    cJSON_AddStringToObject(root,"mode","VARJOTILA_EI_KOMENTOJA");
    cJSON_AddBoolToObject(root,"physical_writes",false);
    cJSON_AddBoolToObject(root,"cn105",valid_cn105(cp,now));
    cJSON_AddStringToObject(root,"pump_mode",cp.mode==ObservedPumpMode::Heat?
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
    web.on("/",HTTP_GET,[](){web.send(200,"text/html; charset=utf-8",html);});
    web.on("/api/status",HTTP_GET,status_endpoint);
    web.on("/api/plan",HTTP_GET,plan_endpoint);
    c23_register_routes(web); // mode/config persistence, NO actuator writes
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
    // BLE already running. No physical actuator command is ever sent.
    xTaskCreatePinnedToCore(optimizer_worker,"cottage_optimizer",16384,nullptr,1,&task_optimizer,1);
    if(cottage_ota_configured())
        xTaskCreatePinnedToCore(cottage_ota_worker,"cottage_ota",16384,nullptr,1,&task_ota,1);
    Serial.println("ENEVINA LITE C2 SHADOW: NO ACTUATOR PATH");
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
