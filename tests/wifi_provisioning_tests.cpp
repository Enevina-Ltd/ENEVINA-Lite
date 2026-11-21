#include "cottage/wifi_provisioning.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace enevina::cottage;

static WifiCredentials creds(const char* s,const char* p) {
    WifiCredentials c{};
    std::strcpy(c.ssid,s);std::strcpy(c.password,p);
    return c;
}
static void first_boot_ap_then_success() {
    WifiProvisioning w;
    w.begin(nullptr,0);
    assert(w.ap_should_run() && w.phase(0)==WifiPhase::Setup);
    assert(w.take_connect_request()==ConnectTarget::None);
    w.tick(1000,false,0);
    assert(w.ap_should_run());
    assert(w.submit(creds("Koti","salasana123"),2000));
    assert(w.phase(2000)==WifiPhase::Trying && w.ap_should_run());
    assert(w.take_connect_request()==ConnectTarget::Candidate);
    assert(!w.take_commit()); // nothing persisted before a verified join
    w.tick(6000,true,0);
    assert(w.last_result()==WifiResult::Connected);
    assert(w.take_commit() && !w.take_commit());
    w.finish_commit(true,6000);
    assert(w.storage()==WifiStorage::Saved && std::strcmp(w.stored().ssid,"Koti")==0);
    // AP stays during the grace period so the phone sees the result...
    w.tick(6000+60000,true,0);
    assert(w.ap_should_run() && w.phase(66000)==WifiPhase::Online);
    // ...and then closes: the setup network is not left open needlessly.
    w.tick(6000+120000,true,0);
    assert(!w.ap_should_run());
}
static void wrong_password_keeps_ap_and_retries() {
    WifiProvisioning w;
    w.begin(nullptr,0);
    assert(w.submit(creds("Koti","vaarasalasana"),1000));
    w.take_connect_request();
    w.tick(3000,false,15); // 4-way handshake timeout = wrong WPA2 passphrase
    assert(w.last_result()==WifiResult::WrongPassword);
    assert(w.phase(3000)==WifiPhase::Setup && w.ap_should_run());
    assert(!w.take_commit());
    assert(w.take_connect_request()==ConnectTarget::Disconnect);
    // A new attempt is possible immediately, without USB.
    assert(w.submit(creds("Koti","oikeasalasana"),4000));
    assert(w.take_connect_request()==ConnectTarget::Candidate);
    w.tick(9000,true,0);
    assert(w.take_commit());
    w.finish_commit(true,9000);
    assert(std::strcmp(w.stored().password,"oikeasalasana")==0);
}
static void not_found_and_timeout() {
    WifiProvisioning w;
    w.begin(nullptr,0);
    w.submit(creds("Naapuri","salasana123"),0);
    w.take_connect_request();
    w.tick(2000,false,201);
    assert(w.last_result()==WifiResult::NetworkNotFound);
    w.submit(creds("Koti","salasana123"),3000);
    w.take_connect_request();
    w.tick(3000,false,0);
    w.tick(3000+24999,false,0);
    assert(w.phase(27999)==WifiPhase::Trying);
    w.tick(3000+25000,false,0);
    assert(w.last_result()==WifiResult::Timeout && w.ap_should_run());
}
static void failed_change_restores_old_network() {
    WifiProvisioning w;
    const auto old=creds("Vanha","vanhasalasana");
    w.begin(&old,0);
    assert(!w.ap_should_run());
    assert(w.take_connect_request()==ConnectTarget::Stored);
    w.tick(5000,true,0);
    assert(w.phase(5000)==WifiPhase::Online);
    assert(w.submit(creds("Uusi","vaarasalasana"),10000));
    assert(w.ap_should_run());
    w.take_connect_request();
    w.tick(12000,false,204);
    assert(w.last_result()==WifiResult::WrongPassword);
    assert(w.take_connect_request()==ConnectTarget::Stored);
    assert(std::strcmp(w.stored().ssid,"Vanha")==0 && !w.take_commit());
    w.tick(20000,true,0);
    w.tick(20000+120000,true,0);
    assert(!w.ap_should_run());
}
static void outage_opens_recovery_ap() {
    WifiProvisioning w;
    const auto old=creds("Koti","salasana123");
    w.begin(&old,0);
    w.tick(1000,true,0);
    w.tick(2000,false,200);  // beacon lost
    assert(!w.ap_should_run() && w.phase(2000)==WifiPhase::Reconnecting);
    w.tick(1000+5*60*1000-1,false,200);
    assert(!w.ap_should_run());
    w.tick(1000+5*60*1000,false,200);
    assert(w.ap_should_run() && w.phase(1000+5*60*1000)==WifiPhase::Recovery);
    // Router back: AP closes after the grace period, no reboot involved.
    const uint32_t back=400000;
    w.tick(back,true,0);
    assert(w.ap_should_run());
    w.tick(back+120000,true,0);
    assert(!w.ap_should_run());
    // Stored network that never connects at boot also reaches recovery.
    WifiProvisioning cold;
    cold.begin(&old,0);
    cold.tick(5*60*1000,false,201);
    assert(cold.ap_should_run());
}
static void validation_and_classification() {
    assert(!valid_ssid("") && valid_ssid("a") && !valid_ssid("123456789012345678901234567890123"));
    assert(!valid_ssid("bad\nssid") && !valid_ssid(nullptr));
    assert(valid_wifi_password("") && !valid_wifi_password("short") && valid_wifi_password("12345678"));
    char hex[65];std::memset(hex,'a',64);hex[64]=0;
    assert(valid_wifi_password(hex));
    hex[3]='z';
    assert(!valid_wifi_password(hex));
    WifiProvisioning w;
    w.begin(nullptr,0);
    assert(!w.submit(creds("","salasana123"),0) && w.phase(0)==WifiPhase::Setup);
    assert(classify_disconnect_reason(202)==WifiResult::WrongPassword);
    assert(classify_disconnect_reason(8)==WifiResult::Failed);
    assert(std::strcmp(wifi_result_name(WifiResult::WrongPassword),"WRONG_PASSWORD")==0);
}
static void addresses_without_mdns() {
    const uint8_t ip[4]={192,168,1,23};
    LocalAddresses a{};
    assert(local_addresses(true,"enevina-lite-ab12",ip,a));
    assert(!std::strcmp(a.primary,"http://enevina-lite.local/"));
    assert(!std::strcmp(a.unique,"http://enevina-lite-ab12.local/"));
    assert(!std::strcmp(a.numeric,"http://192.168.1.23/"));
    // mDNS failed: the numeric address is still offered.
    assert(local_addresses(false,"enevina-lite-ab12",ip,a));
    assert(!a.primary[0] && !a.unique[0] && !std::strcmp(a.numeric,"http://192.168.1.23/"));
    const uint8_t none[4]={0,0,0,0};
    assert(!local_addresses(true,"x",none,a));
}
// Codex P1 regressions (PR #30): the firmware loop may tick() before it has
// consumed the connect request. Old-connection evidence must not decide.
static void online_submit_not_decided_by_old_connection() {
    WifiProvisioning w;
    const auto old=creds("Vanha","vanhasalasana");
    w.begin(&old,0);
    w.take_connect_request();
    w.tick(1000,true,0);
    assert(w.submit(creds("Uusi","vaarasalasana"),2000));
    w.tick(2010,true,0);            // still the OLD network's IP
    assert(!w.take_commit() && w.phase(2010)==WifiPhase::Trying);
    assert(w.take_connect_request()==ConnectTarget::Candidate);
    w.tick(2020,false,204);         // new attempt: wrong password
    assert(w.last_result()==WifiResult::WrongPassword && !w.take_commit());
    assert(std::strcmp(w.stored().ssid,"Vanha")==0);
    assert(w.take_connect_request()==ConnectTarget::Stored);
}
static void retry_not_decided_by_previous_failure_reason() {
    WifiProvisioning w;
    w.begin(nullptr,0);
    w.submit(creds("Koti","vaarasalasana"),0);
    w.take_connect_request();
    w.tick(1000,false,15);
    assert(w.last_result()==WifiResult::WrongPassword);
    assert(w.take_connect_request()==ConnectTarget::Disconnect);
    assert(w.submit(creds("Koti","oikeasalasana"),2000));
    w.tick(2001,false,15);          // stale reason from the previous attempt
    assert(w.phase(2001)==WifiPhase::Trying);
    assert(w.take_connect_request()==ConnectTarget::Candidate);
    w.tick(5000,true,0);
    assert(w.take_commit());
    w.finish_commit(true,5000);
    assert(std::strcmp(w.stored().password,"oikeasalasana")==0);
    // Timeout counts from the actual start of the attempt.
    w.submit(creds("Muu","salasana123"),10000);
    w.tick(40000,false,0);          // not started yet: no timeout
    assert(w.phase(40000)==WifiPhase::Trying);
    w.take_connect_request();
    w.tick(40000+24999,false,0);
    assert(w.phase(64999)==WifiPhase::Trying);
    w.tick(40000+25000,false,0);
    assert(w.last_result()==WifiResult::Timeout);
}
// Codex P1 (PR #30): the open setup AP exposes only Wi-Fi provisioning.
static void setup_ap_only_provisions_wifi() {
    for(const char* ok:{"/","/setup","/api/wifi/status","/api/wifi/scan",
                        "/api/wifi/connect","/api/wifi/retry-save","/generate_204","/hotspot-detect.html"})
        assert(setup_ap_route_allowed(ok));
    for(const char* no:{"/api/control/arm","/api/settings","/api/status","/api/air/config",
                        "/api/air/bind","/api/plan","/api/prices","/api/ota","/setup/x",
                        "/api/wifi","/api/wifi/connectX",""})
        assert(!setup_ap_route_allowed(no));
    assert(!setup_ap_route_allowed(nullptr));
}
static void failed_save_keeps_fallback_and_can_be_retried() {
    WifiProvisioning w;
    const auto old=creds("Vanha","vanhasalasana");
    w.begin(&old,0);
    assert(w.take_connect_request()==ConnectTarget::Stored);
    w.tick(1000,true,0);
    assert(w.submit(creds("Uusi","uusisalasana"),2000));
    assert(w.take_connect_request()==ConnectTarget::Candidate);
    w.tick(3000,true,0);
    assert(w.last_result()==WifiResult::Connected);
    assert(w.take_commit());
    w.finish_commit(false,3000);
    assert(w.storage()==WifiStorage::Failed && w.phase(3000)==WifiPhase::Online);
    assert(std::strcmp(w.active_network(),"Uusi")==0);
    assert(std::strcmp(w.stored().ssid,"Vanha")==0);
    assert(!w.take_commit()); // no tight loop of flash retries
    w.tick(3000+120000,true,0);
    assert(w.ap_should_run());
    assert(w.retry_commit() && !w.retry_commit());
    assert(w.take_commit());
    w.finish_commit(true,123010);
    assert(w.storage()==WifiStorage::Saved);
    assert(std::strcmp(w.stored().ssid,"Uusi")==0);
    w.tick(123010+119999,true,0);
    assert(w.ap_should_run());
    w.tick(123010+120000,true,0);
    assert(!w.ap_should_run());
}
static void unsaved_attempt_cannot_replace_fallback_on_next_join_failure() {
    WifiProvisioning w;
    const auto old=creds("Vanha","vanhasalasana");
    w.begin(&old,0);
    w.take_connect_request();
    w.tick(100,true,0);
    w.submit(creds("Uusi","uusisalasana"),200);
    w.take_connect_request();
    w.tick(300,true,0);
    assert(w.take_commit());
    w.finish_commit(false,300);
    w.tick(400,false,201);
    assert(!w.retry_commit());
    assert(w.submit(creds("Toinen","vaarasalasana"),500));
    assert(!w.retry_commit());
    w.take_connect_request();
    w.tick(600,false,204);
    assert(w.take_connect_request()==ConnectTarget::Stored);
    assert(std::strcmp(w.stored().ssid,"Vanha")==0 && !w.take_commit());
}
int main() {
    failed_save_keeps_fallback_and_can_be_retried();
    unsaved_attempt_cannot_replace_fallback_on_next_join_failure();
    setup_ap_only_provisions_wifi();
    online_submit_not_decided_by_old_connection();
    retry_not_decided_by_previous_failure_reason();
    first_boot_ap_then_success();
    wrong_password_keeps_ap_and_retries();
    not_found_and_timeout();
    failed_change_restores_old_network();
    outage_opens_recovery_ap();
    validation_and_classification();
    addresses_without_mdns();
    std::puts("wifi_provisioning_tests OK");
}
