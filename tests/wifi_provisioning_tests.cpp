#include "cottage/wifi_provisioning.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
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
    assert(std::strcmp(w.stored().ssid,"Koti")==0);
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
    w.tick(9000,true,0);
    assert(w.take_commit() && std::strcmp(w.stored().password,"oikeasalasana")==0);
}
static void not_found_and_timeout() {
    WifiProvisioning w;
    w.begin(nullptr,0);
    w.submit(creds("Naapuri","salasana123"),0);
    w.tick(2000,false,201);
    assert(w.last_result()==WifiResult::NetworkNotFound);
    w.submit(creds("Koti","salasana123"),3000);
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
int main() {
    first_boot_ap_then_success();
    wrong_password_keeps_ap_and_retries();
    not_found_and_timeout();
    failed_change_restores_old_network();
    outage_opens_recovery_ap();
    validation_and_classification();
    addresses_without_mdns();
    std::puts("wifi_provisioning_tests OK");
}
