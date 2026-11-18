#ifndef COTTAGE_BENCH_MODE
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <cJSON.h>
#include <atomic>
#include <cstring>
#include "cottage_config.hpp"
#include "cottage_wifi.hpp"
#include "cottage_air_service.hpp"
#include "cottage/wifi_provisioning.hpp"

using namespace enevina::cottage;
namespace {
WebServer* server=nullptr;
DNSServer dns;
WifiProvisioning machine;
bool ap_active=false;
bool mdns_started=false;
char ap_ssid[24]="Enevina-Lite";
char unique_host[24]="enevina-lite";
std::atomic<uint16_t> last_reason{0};
std::atomic<bool> got_ip{false};

void on_wifi_event(WiFiEvent_t event,WiFiEventInfo_t info) {
    if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        last_reason.store(info.wifi_sta_disconnected.reason);
        got_ip.store(false);
    } else if(event==ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        got_ip.store(true);
    } else if(event==ARDUINO_EVENT_WIFI_STA_LOST_IP) {
        got_ip.store(false);
    }
}
// SSID and password are ONE versioned NVS blob written with a single
// putBytes(): a reset or failed write can never pair a new SSID with an old
// password (Codex P2, PR #30). NVS replaces a key atomically.
struct StoredWifi {
    uint32_t magic=0x45575731; // "EWW1"
    uint16_t version=1;
    uint16_t size=sizeof(WifiCredentials);
    WifiCredentials credentials{};
};
bool load_stored(WifiCredentials& c) {
    Preferences p;
    bool ok=false;
    if(p.begin("wifi",true)) {
        StoredWifi blob{};
        if(p.getBytesLength("cred1")==sizeof(blob) &&
           p.getBytes("cred1",&blob,sizeof(blob))==sizeof(blob) &&
           blob.magic==StoredWifi{}.magic && blob.version==1 &&
           blob.size==sizeof(WifiCredentials) &&
           memchr(blob.credentials.ssid,0,sizeof(blob.credentials.ssid)) &&
           memchr(blob.credentials.password,0,sizeof(blob.credentials.password))) {
            c=blob.credentials;
            ok=valid_ssid(c.ssid) && valid_wifi_password(c.password);
        }
        p.end();
    }
    if(!ok && strlen(COTTAGE_WIFI_SSID)) {
        // Existing pilot build: compile-time network is only the default.
        strlcpy(c.ssid,COTTAGE_WIFI_SSID,sizeof(c.ssid));
        strlcpy(c.password,COTTAGE_WIFI_PASSWORD,sizeof(c.password));
        ok=valid_ssid(c.ssid) && valid_wifi_password(c.password);
    }
    return ok;
}
bool persist(const WifiCredentials& c) {
    StoredWifi blob{};
    blob.credentials=c;
    Preferences p;
    if(!p.begin("wifi",false)) return false;
    const bool ok=p.putBytes("cred1",&blob,sizeof(blob))==sizeof(blob);
    p.end();
    return ok;
}
void start_ap() {
    WiFi.mode(WIFI_AP_STA);
    // Owner decision 18.11.2026: open setup network, no password. It runs only
    // while needed (no stored network / >5 min outage) and closes ~2 min after
    // a verified join. Max 2 clients: a phone plus one retry.
    if(WiFi.softAP(ap_ssid,nullptr,1,0,2)) {
        dns.setErrorReplyCode(DNSReplyCode::NoError);
        dns.start(53,"*",WiFi.softAPIP());
        ap_active=true;
        if(!machine.has_stored())
            Serial.printf("ENEVINA_LITE_SETUP ssid=%s open url=http://%s/\n",
                ap_ssid,WiFi.softAPIP().toString().c_str());
    }
}
void stop_ap() {
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    ap_active=false;
}
void connect_to(const WifiCredentials& c) {
    last_reason.store(0);
    got_ip.store(false);
    WiFi.disconnect(false,false);
    WiFi.begin(c.ssid,c.password[0]?c.password:nullptr);
}
void send_json(cJSON* root,int code=200) {
    char* body=root?cJSON_PrintUnformatted(root):nullptr;
    cJSON_Delete(root);
    server->sendHeader("Cache-Control","no-store");
    if(!body) {server->send(503,"application/json","{}");return;}
    server->send(code,"application/json; charset=utf-8",body);
    cJSON_free(body);
}
void status_route() {
    const uint32_t now=millis();
    cJSON* root=cJSON_CreateObject();
    if(!root) {send_json(nullptr);return;}
    cJSON_AddStringToObject(root,"phase",wifi_phase_name(machine.phase(now)));
    cJSON_AddStringToObject(root,"last_result",wifi_result_name(machine.last_result()));
    cJSON_AddStringToObject(root,"network",machine.has_stored()?machine.stored().ssid:"");
    cJSON_AddBoolToObject(root,"setup_ap_active",ap_active);
    cJSON_AddStringToObject(root,"setup_ap_ssid",ap_ssid);
    cJSON_AddBoolToObject(root,"mdns",mdns_started);
    LocalAddresses a{};
    const IPAddress ip=WiFi.localIP();
    const uint8_t raw[4]={ip[0],ip[1],ip[2],ip[3]};
    // mDNS advertises only "enevina-lite"; the per-device name is the DHCP
    // hostname shown in the router's client list, not an mDNS name.
    if(got_ip.load() && local_addresses(mdns_started,nullptr,raw,a)) {
        cJSON_AddStringToObject(root,"url_primary",a.primary);
        cJSON_AddStringToObject(root,"url_numeric",a.numeric);
    }
    cJSON_AddStringToObject(root,"dhcp_hostname",unique_host);
    // Same existing per-boot CSRF token as /api/status (no new gate).
    cJSON_AddStringToObject(root,"token",c23_config_token());
    send_json(root);
}
void scan_route() {
    if(machine.phase(millis())==WifiPhase::Trying) {
        server->send(409,"application/json","{\"scanning\":false,\"busy\":true}");
        return;
    }
    const int16_t n=WiFi.scanComplete();
    if(n==WIFI_SCAN_RUNNING) {server->send(200,"application/json","{\"scanning\":true}");return;}
    if(n<0) {
        WiFi.scanNetworks(true,false);
        server->send(200,"application/json","{\"scanning\":true}");
        return;
    }
    cJSON* root=cJSON_CreateObject();
    cJSON* list=root?cJSON_AddArrayToObject(root,"networks"):nullptr;
    if(list) {
        cJSON_AddBoolToObject(root,"scanning",false);
        // Strongest entry per SSID, hidden networks omitted, at most 20.
        int emitted=0;
        for(int i=0;i<n && emitted<20;++i) {
            const String ssid=WiFi.SSID(i);
            if(!ssid.length() || ssid.length()>32) continue;
            bool duplicate_stronger=false;
            for(int j=0;j<n;++j)
                if(j!=i && WiFi.SSID(j)==ssid &&
                   (WiFi.RSSI(j)>WiFi.RSSI(i) || (WiFi.RSSI(j)==WiFi.RSSI(i) && j<i)))
                    {duplicate_stronger=true;break;}
            if(duplicate_stronger) continue;
            cJSON* item=cJSON_CreateObject();
            if(!item) break;
            cJSON_AddStringToObject(item,"ssid",ssid.c_str());
            cJSON_AddNumberToObject(item,"rssi",WiFi.RSSI(i));
            cJSON_AddBoolToObject(item,"secure",WiFi.encryptionType(i)!=WIFI_AUTH_OPEN);
            cJSON_AddItemToArray(list,item);
            ++emitted;
        }
    }
    WiFi.scanDelete();
    send_json(root);
}
void connect_route() {
    if(!server->hasArg("token") ||
       !c23_authorize_local_setting(server->arg("token").c_str())) {
        server->send(403,"application/json","{\"accepted\":false,\"error\":\"TOKEN\"}");
        return;
    }
    WifiCredentials c{};
    const String ssid=server->arg("ssid");
    const String pass=server->arg("password");
    if(ssid.length()>=sizeof(c.ssid) || pass.length()>=sizeof(c.password)) {
        server->send(400,"application/json","{\"accepted\":false,\"error\":\"TOO_LONG\"}");
        return;
    }
    strlcpy(c.ssid,ssid.c_str(),sizeof(c.ssid));
    strlcpy(c.password,pass.c_str(),sizeof(c.password));
    if(!machine.submit(c,millis())) {
        server->send(400,"application/json","{\"accepted\":false,\"error\":\"INVALID\"}");
        return;
    }
    server->send(202,"application/json","{\"accepted\":true}");
}
// Registered FIRST, so it sees every request before any other route. Only
// requests that arrive through the setup AP AND are not Wi-Fi provisioning
// routes are intercepted; the home-network interface is never touched.
class SetupApGuard : public RequestHandler {
public:
    bool canHandle(HTTPMethod,String uri) override { return blocked(uri); }
    bool canUpload(String uri) override { return blocked(uri); }
    bool canRaw(String uri) override { return blocked(uri); }
    bool handle(WebServer& web,HTTPMethod method,String) override {
        if(method==HTTP_GET) {
            web.sendHeader("Location",String("http://")+WiFi.softAPIP().toString()+"/setup");
            web.send(302,"text/plain","");
        } else {
            web.send(403,"application/json","{\"error\":\"SETUP_NETWORK_WIFI_ONLY\"}");
        }
        return true;
    }
    void upload(WebServer&,String,HTTPUpload&) override {} // discard
    void raw(WebServer&,String,HTTPRaw&) override {}
private:
    static bool blocked(const String& uri) {
        return server && cottage_wifi_request_via_ap(*server) &&
            !setup_ap_route_allowed(uri.c_str());
    }
};
void captive_redirect() {
    server->sendHeader("Location",String("http://")+WiFi.softAPIP().toString()+"/setup");
    server->send(302,"text/plain","");
}
} // namespace

bool cottage_wifi_request_via_ap(WebServer& web) {
    return ap_active && web.client().localIP()==WiFi.softAPIP();
}
void cottage_wifi_begin(WebServer& web) {
    server=&web;
    // Must be the first handler: the open setup AP may only provision Wi-Fi.
    web.addHandler(new SetupApGuard());
    uint8_t mac[6]{};
    WiFi.macAddress(mac);
    snprintf(ap_ssid,sizeof(ap_ssid),"Enevina-Lite-%02X%02X",mac[4],mac[5]);
    snprintf(unique_host,sizeof(unique_host),"enevina-lite-%02x%02x",mac[4],mac[5]);
    WiFi.persistent(false);          // credentials live in our own NVS namespace
    WiFi.onEvent(on_wifi_event);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(unique_host);   // distinct DHCP name per device
    WiFi.setAutoReconnect(true);
    WifiCredentials stored{};
    const bool have=load_stored(stored);
    machine.begin(have?&stored:nullptr,millis());
    web.on("/setup",HTTP_GET,[]() {
        server->sendHeader("Cache-Control","no-store");
        server->send_P(200,"text/html; charset=utf-8",cottage_wifi_setup_html);
    });
    web.on("/api/wifi/status",HTTP_GET,status_route);
    web.on("/api/wifi/scan",HTTP_GET,scan_route);
    web.on("/api/wifi/connect",HTTP_POST,connect_route);
    // Android / iOS / Windows captive-portal probes open the setup page.
    for(const char* probe:{"/generate_204","/gen_204","/hotspot-detect.html",
                           "/library/test/success.html","/connecttest.txt","/ncsi.txt"})
        web.on(probe,HTTP_GET,captive_redirect);
    cottage_wifi_loop();
}
void cottage_wifi_loop() {
    if(!server) return;
    const uint32_t now=millis();
    if(ap_active) dns.processNextRequest();
    // Start any pending attempt FIRST (resets got_ip / last_reason), then
    // evaluate: the old connection's evidence never decides a new candidate.
    switch(machine.take_connect_request()) {
    case ConnectTarget::Stored: connect_to(machine.stored()); break;
    case ConnectTarget::Candidate: connect_to(machine.candidate()); break;
    case ConnectTarget::Disconnect: WiFi.disconnect(false,false); break;
    case ConnectTarget::None: break;
    }
    machine.tick(now,got_ip.load(),last_reason.load());
    if(machine.take_commit() && !persist(machine.stored()))
        Serial.println("WIFI_WARN: verified network could not be stored in NVS");
    if(machine.ap_should_run() && !ap_active) start_ap();
    else if(!machine.ap_should_run() && ap_active) stop_ap();
    if(got_ip.load() && !mdns_started) {
        // mDNS is optional: the numeric address is always shown as fallback.
        if(MDNS.begin("enevina-lite")) {
            MDNS.addService("http","tcp",80);
            MDNS.addServiceTxt("http","tcp","device",String(unique_host));
            mdns_started=true;
        }
    }
}
#endif // COTTAGE_BENCH_MODE
