#pragma once
// Wi-Fi-first commissioning, phase 1 (pure logic, host-tested).
//
// Phase 1 asks ONLY for the home Wi-Fi. Everything else is configured later
// in the browser on the home network (cottage/site_settings.hpp).
//
//  * No stored network            -> provisioning AP on.
//  * Candidate submitted          -> try it with the AP still on; commit to
//                                    NVS ONLY after an IP address is obtained.
//  * Candidate fails              -> classified error (wrong password / not
//                                    found / timeout), old network restored,
//                                    AP stays on for a new attempt.
//  * Connected                    -> AP closes after a short grace period so
//                                    the phone can read the success page.
//  * Stored network lost > N min  -> AP reopens for recovery (no USB), while
//                                    the station keeps retrying in parallel.
//
// This is commissioning, not a watchdog: it never restarts the device and
// never touches pump control. The firmware applies `ap_should_run()`
// idempotently and executes `take_connect_request()`.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace enevina::cottage {
struct WifiCredentials {
    char ssid[33]{};
    char password[65]{};
};
enum class WifiResult : uint8_t {
    None, Connected, WrongPassword, NetworkNotFound, Timeout, Failed
};
enum class WifiPhase : uint8_t {
    Setup,       // no stored network, waiting for the user
    Trying,      // testing a submitted candidate
    Online,      // stored network connected
    Reconnecting,// stored network lost, still within the recovery delay
    Recovery     // stored network lost long enough: AP open again
};
struct WifiTimings {
    uint32_t candidate_timeout_ms=25000;
    uint32_t ap_close_grace_ms=120000;
    uint32_t recovery_after_ms=5u*60u*1000u;
};
enum class ConnectTarget : uint8_t { None, Stored, Candidate, Disconnect };

inline bool valid_ssid(const char* s) {
    if(!s) return false;
    const size_t n=std::strlen(s);
    if(n<1 || n>32) return false;
    for(size_t i=0;i<n;++i) if(static_cast<unsigned char>(s[i])<0x20) return false;
    return true;
}
// Empty = open network. WPA2/WPA3 passphrase 8..63 printable ASCII, or 64 hex.
inline bool valid_wifi_password(const char* p) {
    if(!p) return false;
    const size_t n=std::strlen(p);
    if(n==0) return true;
    if(n==64) {
        for(size_t i=0;i<n;++i) {
            const char c=p[i];
            if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return false;
        }
        return true;
    }
    if(n<8 || n>63) return false;
    for(size_t i=0;i<n;++i) if(p[i]<0x20 || p[i]>0x7e) return false;
    return true;
}
// ESP-IDF wifi_err_reason_t -> user-facing class. 15/204 are what a WPA2
// station reports for a wrong passphrase; 202 for an explicit auth reject.
inline WifiResult classify_disconnect_reason(uint16_t reason) {
    switch(reason) {
    case 0: return WifiResult::None;
    case 2: case 15: case 202: case 204: return WifiResult::WrongPassword;
    case 201: return WifiResult::NetworkNotFound;
    default: return WifiResult::Failed;
    }
}
inline const char* wifi_result_name(WifiResult r) {
    switch(r) {
    case WifiResult::None:return "NONE";
    case WifiResult::Connected:return "CONNECTED";
    case WifiResult::WrongPassword:return "WRONG_PASSWORD";
    case WifiResult::NetworkNotFound:return "NETWORK_NOT_FOUND";
    case WifiResult::Timeout:return "TIMEOUT";
    case WifiResult::Failed:return "FAILED";
    }
    return "FAILED";
}
inline const char* wifi_phase_name(WifiPhase p) {
    switch(p) {
    case WifiPhase::Setup:return "SETUP";
    case WifiPhase::Trying:return "TRYING";
    case WifiPhase::Online:return "ONLINE";
    case WifiPhase::Reconnecting:return "RECONNECTING";
    case WifiPhase::Recovery:return "RECOVERY";
    }
    return "SETUP";
}

class WifiProvisioning {
public:
    explicit WifiProvisioning(WifiTimings t={}) : timings_(t) {}
    // Boot: stored network from NVS (or compile-time default), may be null.
    void begin(const WifiCredentials* stored,uint32_t now_ms) {
        has_stored_=stored && valid_ssid(stored->ssid) && valid_wifi_password(stored->password);
        if(has_stored_) stored_=*stored;
        last_online_ms_=now_ms;
        connect_request_=has_stored_?ConnectTarget::Stored:ConnectTarget::None;
        ap_=!has_stored_;
    }
    // User submitted a network from the setup page. Invalid input is
    // rejected without disturbing the current connection.
    bool submit(const WifiCredentials& c,uint32_t now_ms) {
        if(!valid_ssid(c.ssid) || !valid_wifi_password(c.password)) return false;
        candidate_=c;
        trying_=true;
        try_started_ms_=now_ms;
        result_=WifiResult::None;
        ap_=true;                 // keep the phone's link while testing
        connect_request_=ConnectTarget::Candidate;
        return true;
    }
    // Called every loop with the station state and the LAST disconnect
    // reason recorded since the current attempt began (0 = none).
    void tick(uint32_t now_ms,bool sta_has_ip,uint16_t disconnect_reason) {
        if(trying_) {
            if(sta_has_ip) {
                stored_=candidate_;
                has_stored_=true;
                commit_pending_=true;
                trying_=false;
                result_=WifiResult::Connected;
                online_since_ms_=last_online_ms_=now_ms;
                was_online_=true;
                return;
            }
            const WifiResult r=classify_disconnect_reason(disconnect_reason);
            const bool definitive=r==WifiResult::WrongPassword || r==WifiResult::NetworkNotFound;
            if(definitive || now_ms-try_started_ms_>=timings_.candidate_timeout_ms) {
                trying_=false;
                result_=definitive?r:WifiResult::Timeout;
                // Never lose a working network because of a typo.
                connect_request_=has_stored_?ConnectTarget::Stored:ConnectTarget::Disconnect;
                last_online_ms_=now_ms; // restart the recovery delay
                was_online_=false;
            }
            return;
        }
        if(sta_has_ip) {
            if(!was_online_) online_since_ms_=now_ms;
            was_online_=true;
            last_online_ms_=now_ms;
            if(ap_ && now_ms-online_since_ms_>=timings_.ap_close_grace_ms) ap_=false;
            return;
        }
        was_online_=false;
        if(!has_stored_) {ap_=true;return;}
        if(now_ms-last_online_ms_>=timings_.recovery_after_ms) ap_=true;
    }
    WifiPhase phase(uint32_t now_ms) const {
        if(trying_) return WifiPhase::Trying;
        if(!has_stored_) return WifiPhase::Setup;
        if(was_online_) return WifiPhase::Online;
        return now_ms-last_online_ms_>=timings_.recovery_after_ms ?
            WifiPhase::Recovery:WifiPhase::Reconnecting;
    }
    bool ap_should_run() const { return ap_; }
    // One-shot: the firmware calls WiFi.begin() for this target.
    ConnectTarget take_connect_request() {
        const ConnectTarget t=connect_request_;
        connect_request_=ConnectTarget::None;
        return t;
    }
    // One-shot: persist stored() to NVS now (only after a verified join).
    bool take_commit() { const bool c=commit_pending_; commit_pending_=false; return c; }
    const WifiCredentials& stored() const { return stored_; }
    const WifiCredentials& candidate() const { return candidate_; }
    bool has_stored() const { return has_stored_; }
    WifiResult last_result() const { return result_; }
private:
    WifiTimings timings_;
    WifiCredentials stored_{},candidate_{};
    bool has_stored_=false,trying_=false,ap_=false,commit_pending_=false,was_online_=false;
    uint32_t try_started_ms_=0,last_online_ms_=0,online_since_ms_=0;
    WifiResult result_=WifiResult::None;
    ConnectTarget connect_request_=ConnectTarget::None;
};

// Local addresses shown on the success page. mDNS is preferred but never
// required: the numeric IP is always listed, so a phone without mDNS
// (some Android versions) or a router that blocks multicast still works.
struct LocalAddresses {
    char primary[64]{};   // http://enevina-lite.local/  (only if mDNS started)
    char unique[64]{};    // http://enevina-lite-ab12.local/
    char numeric[32]{};   // http://192.168.1.23/
};
inline bool local_addresses(bool mdns_ok,const char* unique_host,const uint8_t ip[4],
                            LocalAddresses& out) {
    out=LocalAddresses{};
    if(!ip || (ip[0]==0 && ip[1]==0 && ip[2]==0 && ip[3]==0)) return false;
    std::snprintf(out.numeric,sizeof(out.numeric),"http://%u.%u.%u.%u/",ip[0],ip[1],ip[2],ip[3]);
    if(mdns_ok) {
        std::snprintf(out.primary,sizeof(out.primary),"http://enevina-lite.local/");
        if(unique_host && *unique_host)
            std::snprintf(out.unique,sizeof(out.unique),"http://%s.local/",unique_host);
    }
    return true;
}
} // namespace enevina::cottage
