#ifdef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <atomic>
#include "cottage/physics.hpp"
#include "cottage/mpc.hpp"
using namespace enevina::cottage;
static WebServer server(80);
static std::atomic<bool> running{true};
static std::atomic<unsigned> http_ok{0};
static std::atomic<unsigned> http_fail{0};
static std::atomic<unsigned> results_ok{0};
static std::atomic<unsigned> results_fail{0};
static uint32_t worst_ms=0;

static Parameters params(){
    Parameters p{};
    p.thermal_mass_kwh_per_c=2;
    p.loss_kw_per_c=.12;
    p.heat_kw_at_zero_c=3.2;
    p.heat_kw_change_per_outdoor_c=.03;
    p.minimum_heat_kw=1;
    p.cop_at_zero_c=2.8;
    p.cop_change_per_outdoor_c=.04;
    p.minimum_cop=1.2;
    p.maximum_cop=5;
    p.startup_delay_minutes=3;
    p.startup_electric_kwh=.05;
    p.heat_standby_kw=.012;
    p.off_standby_kw=.002;
    p.planning_min_c=8;
    p.critical_c=5;
    p.maximum_indoor_c=14.5;
    p.min_heat_minutes=60;
    p.min_off_minutes=60;
    p.switching_penalty_eur=.03;
    return p;
}
static void wifi_http_probe(void*){
    while(running.load()){
        WiFiClient client;
        if(client.connect(WiFi.softAPIP(),80,1000)) {
            client.print("GET /status HTTP/1.1\r\nHost: 192.168.4.1\r\nConnection: close\r\n\r\n");
            const uint32_t deadline=millis()+1500;
            bool saw200=false;
            while(client.connected() && static_cast<int32_t>(deadline-millis())>0) {
                while(client.available()) {
                    const String line=client.readStringUntil('\n');
                    if(line.startsWith("HTTP/1.1 200"))saw200=true;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            if(saw200)http_ok++;else http_fail++;
            client.stop();
        } else http_fail++;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelete(nullptr);
}
static void bench_task(void*) {
    const auto p=params();
    const State initial{10,false,180};
    bool valid_all=true;
    for(const int n:{96,192}) {
        for(const int scenario:{0,1,2}) {
            std::vector<Period> slots(static_cast<size_t>(n));
            for(int i=0;i<n;i++) {
                const double outdoor=(scenario==1?-10:-4)+2*std::sin(6.283185307179586*i/96);
                double price=(i%96<20 || i%96>=80)?0.07:0.41;
                if(scenario==2 && i%96>=24 && i%96<72)price=0.95;
                slots[static_cast<size_t>(i)]={outdoor,price,0,1};
            }
            const auto now=millis();
            const auto result=optimize(p,initial,slots.data(),slots.size());
            const auto ms=millis()-now;
            worst_ms=std::max(worst_ms,static_cast<uint32_t>(ms));
            if(result.status==MpcStatus::Ok && result.count==static_cast<size_t>(n) &&
               result.minimum_c>=8 && result.final_c>=10)results_ok++;
            else {results_fail++;valid_all=false;}
            Serial.printf("C2_BENCH scenario=%d slots=%d ms=%u status=%d cost=%.6f starts=%u min=%.3f final=%.3f heap=%u psram=%u stack_free_bytes=%u\n",
                scenario,n,ms,static_cast<int>(result.status),
                result.cost_eur,result.starts,result.minimum_c,result.final_c,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                uxTaskGetStackHighWaterMark(nullptr));
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
    Serial.printf("C2_BENCH_RESULT %s runs_pass=%u runs_fail=%u worst_ms=%u http_ok=%u http_fail=%u internal_heap_low=%u psram_low=%u\n",
        valid_all?"PASS":"FAIL",results_ok.load(),results_fail.load(),worst_ms,
        http_ok.load(),http_fail.load(),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    running=false;
    WiFi.softAPdisconnect(true);
    vTaskDelete(nullptr);
}
void setup(){
    Serial.begin(115200);
    delay(500);
    char key[17]={};
    const char* hex="0123456789ABCDEF";
    for(int i=0;i<16;i++)key[i]=hex[esp_random()&0xf];
    WiFi.mode(WIFI_AP);
    const bool ap=WiFi.softAP("ENEVINA_C2_BENCH",key,1,true,1);
    Serial.printf("C2_BENCH_START AP=%d heap=%u psram=%u freq=%u\n",
        ap,heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),ESP.getCpuFreqMHz());
    server.on("/status",HTTP_GET,[](){server.send(200,"text/plain","SHADOW_ONLY");});
    server.begin();
    xTaskCreatePinnedToCore(wifi_http_probe,"http_probe",6144,nullptr,1,nullptr,0);
    xTaskCreatePinnedToCore(bench_task,"bench_task",32768,nullptr,2,nullptr,1);
}
void loop(){server.handleClient();delay(10);}
#endif
