#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include "safety/supervisor.hpp"
#include "pump/heishamon_adapter.hpp"

namespace {
constexpr char tag[] = "enevina_shadow";
std::atomic<bool> ethernet_ready{false};
SemaphoreHandle_t telemetry_mutex;
struct Telemetry {
    bool valid=false;
    float inlet_c=NAN;
    float outlet_c=NAN;
    float outdoor_c=NAN;
    float dhw_c=NAN;
    float compressor_hz=NAN;
    int defrosting=-1;
    int operating_mode=-1;
    int64_t received_utc=0;
};
Telemetry telemetry;
// Default constructor is always read-only. This firmware has no device-write path.
enevina::Supervisor supervisor;

void on_ip(void*, esp_event_base_t, int32_t, void* data) {
    auto* event=static_cast<ip_event_got_ip_t*>(data);
    ethernet_ready.store(true);
    ESP_LOGI(tag,"Ethernet IPv4: " IPSTR, IP2STR(&event->ip_info.ip));
}
void on_eth(void*, esp_event_base_t, int32_t id, void*) {
    if (id==ETHERNET_EVENT_DISCONNECTED || id==ETHERNET_EVENT_STOP)
        ethernet_ready.store(false);
}

bool read_topic(const char* base, const char* topic, double& value) {
    char url[192];
    const int n=std::snprintf(url,sizeof(url),"%s/json?topic=%s",base,topic);
    if (n<0 || n>=static_cast<int>(sizeof(url))) return false;
    esp_http_client_config_t cfg{};
    cfg.url=url;
    cfg.timeout_ms=1800;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if (!client) return false;
    bool ok=false;
    if (esp_http_client_open(client,0)==ESP_OK) {
        esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client)==200) {
            char body[192];
            const int length=esp_http_client_read_response(client,body,sizeof(body)-1);
            if (length>0 && length<static_cast<int>(sizeof(body))) {
                body[length]='\0';
                cJSON* root=cJSON_Parse(body);
                if (root) {
                    cJSON* item=cJSON_GetObjectItemCaseSensitive(root,"Value");
                    if (cJSON_IsNumber(item) && std::isfinite(item->valuedouble)) {
                        value=item->valuedouble;
                        ok=true;
                    }
                    cJSON_Delete(root);
                }
            }
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return ok;
}

class HttpTopicReader final : public enevina::TopicReader {
public:
    explicit HttpTopicReader(const char* base):base_(base) {}
    bool read_number(const char* topic, double& value) override {
        return read_topic(base_,topic,value);
    }
private:
    const char* base_;
};

void poll_heishamon(void*) {
    const char* base=CONFIG_ENEVINA_HEISHAMON_BASE_URL;
    HttpTopicReader reader(base);
    enevina::HeishaMonAdapter adapter(reader);
    while (true) {
        if (ethernet_ready.load() && std::strncmp(base,"http://",7)==0) {
            enevina::PumpTelemetry reading{};
            const bool ok=adapter.poll(reading);
            Telemetry next;
            next.valid=ok;
            if (next.valid) {
                next.inlet_c=reading.inlet_c; next.outlet_c=reading.outlet_c;
                next.outdoor_c=reading.outdoor_c; next.dhw_c=reading.dhw_c;
                next.compressor_hz=reading.compressor_hz;
                next.defrosting=reading.defrosting?1:0;
                next.operating_mode=reading.operating_mode;
                next.received_utc=static_cast<int64_t>(time(nullptr));
            }
            xSemaphoreTake(telemetry_mutex,portMAX_DELAY);
            telemetry=next;
            xSemaphoreGive(telemetry_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

esp_err_t status_handler(httpd_req_t* req) {
    Telemetry snapshot;
    xSemaphoreTake(telemetry_mutex,portMAX_DELAY);
    snapshot=telemetry;
    xSemaphoreGive(telemetry_mutex);
    const int64_t now=static_cast<int64_t>(time(nullptr));
    const bool fresh=snapshot.valid && snapshot.received_utc>0 &&
        now>=snapshot.received_utc && now-snapshot.received_utc<=120;
    char body[400];
    std::snprintf(body,sizeof(body),
        "{\"mode\":\"shadow\",\"pump_connected\":%s,\"telemetry_utc\":%lld,"
        "\"inlet_c\":%.1f,\"outlet_c\":%.1f,\"outdoor_c\":%.1f,"
        "\"dhw_c\":%.1f,\"compressor_hz\":%.0f,\"defrosting\":%d,"
        "\"operating_mode\":%d,\"forecast_status\":\"missing\","
        "\"reason\":\"Ennuste ja asennuskohtainen kalibrointi puuttuvat\"}",
        fresh?"true":"false",static_cast<long long>(snapshot.received_utc),
        fresh?snapshot.inlet_c:0, fresh?snapshot.outlet_c:0,
        fresh?snapshot.outdoor_c:0, fresh?snapshot.dhw_c:0,
        fresh?snapshot.compressor_hz:0, fresh?snapshot.defrosting:-1,
        fresh?snapshot.operating_mode:-1);
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,body);
}

constexpr char page[] = R"HTML(<!doctype html><html lang="fi"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Enevina Lite</title><style>
body{font:16px system-ui,sans-serif;background:#f4f7f6;color:#12352d;margin:0;padding:22px}
main{max-width:720px;margin:auto}h1{font-size:1.7rem}section{background:white;border-radius:14px;
padding:20px;margin:14px 0;box-shadow:0 1px 8px #12352d14}dl{display:grid;
grid-template-columns:1fr auto;gap:10px}dt,dd{margin:0}dd{font-weight:700}
.pill{display:inline-block;background:#fff2d1;color:#705008;padding:7px 12px;border-radius:30px}
</style><main><h1>Enevina Lite</h1><p class="pill">Vain seuranta — ei laitekirjoituksia</p>
<section><h2>Lämpöpumppu</h2><p id="connection">Ladataan…</p><dl>
<dt>Menovesi</dt><dd id="outlet">—</dd><dt>Paluuvesi</dt><dd id="inlet">—</dd>
<dt>Ulkolämpötila</dt><dd id="outdoor">—</dd><dt>Käyttövesi</dt><dd id="dhw">—</dd>
<dt>Kompressori</dt><dd id="compressor">—</dd><dt>Sulatus</dt><dd id="defrost">—</dd>
</dl></section><section><h2>Optimointi</h2><p>Turvallinen varatila: pumpun oma ohjaus.</p>
<p id="reason">Ennuste ja asennuskohtainen kalibrointi puuttuvat.</p></section>
<script>async function update(){try{let r=await fetch('/api/status',{cache:'no-store'});
let x=await r.json();let ok=x.pump_connected;document.querySelector('#connection').textContent=
ok?'Mittaukset käytettävissä':'Lämpöpumpun mittaukset puuttuvat tai ovat vanhentuneet';
for(let k of ['outlet','inlet','outdoor','dhw'])document.querySelector('#'+k).textContent=
ok?x[k+'_c'].toFixed(1)+' °C':'—';document.querySelector('#compressor').textContent=
ok?x.compressor_hz+' Hz':'—';document.querySelector('#defrost').textContent=
ok?(x.defrosting?'Käynnissä':'Ei'):'—';document.querySelector('#reason').textContent=x.reason;
}catch(e){document.querySelector('#connection').textContent='Yhteys laitteeseen katkesi';}}
update();setInterval(update,10000);</script></main></html>)HTML";

esp_err_t page_handler(httpd_req_t* req) {
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,page);
}

void start_web() {
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.stack_size=6144;
    cfg.max_uri_handlers=4;
    httpd_handle_t server=nullptr;
    ESP_ERROR_CHECK(httpd_start(&server,&cfg));
    httpd_uri_t root{}; root.uri="/"; root.method=HTTP_GET; root.handler=page_handler;
    httpd_uri_t status{}; status.uri="/api/status"; status.method=HTTP_GET;
    status.handler=status_handler;
    ESP_ERROR_CHECK(httpd_register_uri_handler(server,&root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server,&status));
}

void start_ethernet() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    spi_bus_config_t bus{};
    bus.miso_io_num=12; bus.mosi_io_num=11; bus.sclk_io_num=13;
    bus.quadwp_io_num=-1; bus.quadhd_io_num=-1;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    spi_device_interface_config_t device{};
    device.mode=0; device.clock_speed_hz=20*1000*1000;
    device.queue_size=20; device.spics_io_num=14;
    eth_mac_config_t mac_cfg=ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg=ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr=0; phy_cfg.reset_gpio_num=9;
    eth_w5500_config_t chip_cfg=ETH_W5500_DEFAULT_CONFIG(SPI2_HOST,&device);
    chip_cfg.int_gpio_num=10;
    esp_eth_mac_t* mac=esp_eth_mac_new_w5500(&chip_cfg,&mac_cfg);
    esp_eth_phy_t* phy=esp_eth_phy_new_w5500(&phy_cfg);
    ESP_ERROR_CHECK(mac && phy ? ESP_OK : ESP_ERR_NO_MEM);
    esp_eth_config_t eth_cfg=ETH_DEFAULT_CONFIG(mac,phy);
    esp_eth_handle_t handle=nullptr;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg,&handle));
    uint8_t mac_address[6];
    ESP_ERROR_CHECK(esp_read_mac(mac_address,ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(handle,ETH_CMD_S_MAC_ADDR,mac_address));
    esp_netif_config_t net_cfg=ESP_NETIF_DEFAULT_ETH();
    esp_netif_t* netif=esp_netif_new(&net_cfg);
    ESP_ERROR_CHECK(netif ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_attach(netif,esp_eth_new_netif_glue(handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT,ESP_EVENT_ANY_ID,on_eth,nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_ETH_GOT_IP,on_ip,nullptr));
    ESP_ERROR_CHECK(esp_eth_start(handle));
}
}

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    telemetry_mutex=xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(telemetry_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    start_ethernet();
    start_web();
    xTaskCreate(poll_heishamon,"heishamon_read",6144,nullptr,4,nullptr);
    ESP_LOGW(tag,"read-only shadow firmware: no heat-pump write endpoint");
}
