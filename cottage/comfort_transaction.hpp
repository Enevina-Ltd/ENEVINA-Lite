#pragma once
// Recoverable Comfort wizard journal: a single durable NVS key tracks the
// transition from PREPARED (no air mutation allowed) to SITE_COMMITTED.
// The predecessor's settings are retained so an interrupted update can be
// rolled back without inventing a target or discarding unrelated site fields.
#include "cottage/site_settings.hpp"
#include "cottage/air_humidity.hpp"
#include <cmath>
#include <cstdint>

namespace enevina::cottage {
// A claimed hard limit on the wrong side of a thermostat setpoint is not
// optional optimization headroom. Never silently ignore a user-entered bound.
inline bool comfort_limits_fit_target(double lo,double hi,double target) {
    return std::isfinite(target) && valid_comfort_limits(lo,hi) &&
           (lo==0 || lo<=target-0.5+1e-5) &&
           (hi==0 || hi>=target+0.5-1e-5);
}
inline bool mode_comfort_limits_fit(const SiteSettings& site,
                                   const CottageModeConfig& cfg) {
    switch(cfg.mode) {
    case CottageMode::Hold:
        return comfort_limits_fit_target(site.comfort_min_c,site.comfort_max_c,
                                         cfg.hold_target_c);
    case CottageMode::Occupied:
        return comfort_limits_fit_target(site.comfort_min_c,site.comfort_max_c,
                                         cfg.occupied_target_c);
    default:
        // Cold has no active heating target; Drying computes a separate
        // humidity/outdoor-dependent target and no price headroom is promised.
        return true;
    }
}
enum class ComfortPhase : uint8_t { Prepared=0, SiteCommitted=1 };
struct ComfortTxn {
    uint32_t magic=0x43545831;   // CTX1, version 2
    uint16_t version=2;
    uint8_t mode=2;             // Desired CottageMode
    uint8_t profile=1;
    uint8_t phase=static_cast<uint8_t>(ComfortPhase::Prepared);
    uint8_t previous_profile=1;
    uint8_t previous_setup_step=5;
    uint8_t previous_mode_plus_one=0; // Zero in existing target-only journals
    float target=12;
    float min_c=0,max_c=0;
    float previous_target=12;
    float previous_min_c=0,previous_max_c=0;
};
inline bool valid_comfort_txn(const ComfortTxn& t) {
    if(t.magic!=0x43545831 || t.version!=2 || t.mode>3 ||
       t.profile>2 || t.previous_profile>2 || t.previous_setup_step>9 ||
       t.phase>static_cast<uint8_t>(ComfortPhase::SiteCommitted) ||
       t.previous_mode_plus_one>4 || (t.mode<2 && t.previous_mode_plus_one==0) ||
       !std::isfinite(t.target) ||
       !std::isfinite(t.previous_target) ||
       !(t.mode>=2?comfort_limits_fit_target(t.min_c,t.max_c,t.target):
                    valid_comfort_limits(t.min_c,t.max_c)) ||
       !valid_comfort_limits(t.previous_min_c,t.previous_max_c))return false;
    if(t.mode<2 && std::fabs(t.target-t.previous_target)>=0.001)return false;
    return t.mode!=3 ?
        t.target>=5&&t.target<=24&&
        t.previous_target>=5&&t.previous_target<=24 :
        t.target>=16&&t.target<=26&&
        t.previous_target>=16&&t.previous_target<=26&&
        std::fabs(t.target*2-std::round(t.target*2))<1e-5 &&
        std::fabs(t.previous_target*2-std::round(t.previous_target*2))<1e-5;
}
inline bool comfort_site_matches(const ComfortTxn& t,const SiteSettings& s) {
    return valid_comfort_txn(t) &&
        t.phase==static_cast<uint8_t>(ComfortPhase::SiteCommitted) &&
        s.setup_step==6 &&
        static_cast<uint8_t>(s.profile)==t.profile &&
        std::fabs(s.comfort_min_c-t.min_c)<0.001 &&
        std::fabs(s.comfort_max_c-t.max_c)<0.001;
}
inline bool comfort_old_site_matches(const ComfortTxn& t,const SiteSettings& s) {
    return valid_comfort_txn(t) &&
        s.setup_step==t.previous_setup_step &&
        static_cast<uint8_t>(s.profile)==t.previous_profile &&
        std::fabs(s.comfort_min_c-t.previous_min_c)<0.001 &&
        std::fabs(s.comfort_max_c-t.previous_max_c)<0.001;
}
inline bool comfort_mode_matches(const ComfortTxn& t,const CottageModeConfig& c) {
    if(!valid_comfort_txn(t) || static_cast<uint8_t>(c.mode)!=t.mode)return false;
    const double current=t.mode==3?c.occupied_target_c:c.hold_target_c;
    return std::fabs(current-t.target)<0.001;
}
inline bool comfort_old_mode_matches(const ComfortTxn& t,const CottageModeConfig& c) {
    const uint8_t previous_mode=t.previous_mode_plus_one?
        static_cast<uint8_t>(t.previous_mode_plus_one-1):t.mode;
    if(!valid_comfort_txn(t) || static_cast<uint8_t>(c.mode)!=previous_mode)return false;
    const double current=t.mode==3?c.occupied_target_c:c.hold_target_c;
    return std::fabs(current-t.previous_target)<0.001;
}
// Pure recovery classification used by firmware AND host fault matrix. An
// old-site matching target-only transaction is NEVER committed while PREPARED.
enum class ComfortRecoveryAction : uint8_t { Block, RestorePriorSite, CompleteAirTarget };
inline ComfortRecoveryAction comfort_recovery_action(const ComfortTxn& tx,
    const SiteSettings& site,const CottageModeConfig& air,bool air_durable) {
    if(!air_durable || !valid_comfort_txn(tx))
        return ComfortRecoveryAction::Block;
    const uint8_t mode=static_cast<uint8_t>(air.mode);
    const double target=tx.mode==3?air.occupied_target_c:air.hold_target_c;
    // Mode and target are separate air keys. Either can already be durable.
    const bool known_mode=mode==tx.mode ||
        (tx.previous_mode_plus_one && mode+1==tx.previous_mode_plus_one);
    const bool known_target=std::fabs(target-tx.previous_target)<0.001 ||
        std::fabs(target-tx.target)<0.001;
    if(!comfort_site_matches(tx,site) || !known_mode || !known_target)
        // A rollback-firmware target edit newer than both journal endpoints
        // belongs to the owner. Never replay the stale target over it.
        return ComfortRecoveryAction::RestorePriorSite;
    return ComfortRecoveryAction::CompleteAirTarget;
}
inline void comfort_apply_target(const ComfortTxn& t,CottageModeConfig& c) {
    c.mode=static_cast<CottageMode>(t.mode);
    if(t.mode==3)c.occupied_target_c=t.target;
    else if(t.mode==2)c.hold_target_c=t.target;
}
}
