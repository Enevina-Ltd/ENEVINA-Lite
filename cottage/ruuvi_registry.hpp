#pragma once
// RuuviTag discovery table and owner-given sensor names.
//
// The BLE callback feeds every decoded, plausible Data Format 5 frame into the
// registry so the setup UI can show a LIST of nearby sensors instead of only the
// most recent one. Pure C++ (no BLE, NVS or HTTP): host-tested; the firmware
// wires the callback, the web routes and persistence.
//
// Rules: a fixed number of slots (no allocation); an unseen sensor is dropped
// after kRuuviForgetMs; when full, the least recently seen entry is replaced,
// never the freshest; all values shown come from the latest accepted frame of
// that MAC with its age - a sensor that stopped advertising keeps its old age
// and is never presented as fresh.
//
// Hearing a sensor is not the same as having a measurement: a sensor that keeps
// repeating the same (or an older) frame sequence is "heard" but delivers no new
// measurement, exactly as RuuviBinding::accept() treats it (Duplicate /
// OldSequence). `last_ms` is when ANY frame arrived; `measured_ms` and `measured`
// are the last frame whose sequence really advanced. Freshness shown to the
// owner follows the measurement, so setup never calls a replaying sensor "live".
#include "cottage/ruuvi_v5.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enevina::cottage {
constexpr size_t kRuuviRegistrySlots=8;
constexpr uint64_t kRuuviForgetMs=15ull*60*1000;   // unseen for 15 min: gone
constexpr uint64_t kRuuviFreshMs=2ull*60*1000;     // "live" if seen within 2 min

struct RuuviSeen {
    RuuviV5 latest{};        // last frame heard (signal strength, MAC)
    RuuviV5 measured{};      // last frame with an advancing sequence
    uint64_t first_ms=0;
    uint64_t last_ms=0;      // last frame heard
    uint64_t measured_ms=0;  // last accepted measurement
    uint64_t restart_ms=0;   // untrusted restart candidate (battery change)
    uint32_t frames=0;
    uint16_t restart_sequence=0;
    bool has_measurement=false;
    bool restart_candidate=false;
    bool used=false;
};
// Same sequence arithmetic as RuuviBinding::accept (modulo 65535).
inline unsigned ruuvi_sequence_distance(uint16_t from,uint16_t to) {
    return (static_cast<unsigned>(to)+65535u-static_cast<unsigned>(from))%65535u;
}

class RuuviRegistry {
public:
    // Returns false for frames that must not even be listed.
    bool observe(const RuuviV5& s,uint64_t now_ms) {
        if(!std::isfinite(s.temperature_c)||s.temperature_c<-45||s.temperature_c>85||
           !std::isfinite(s.relative_humidity_pct)||s.relative_humidity_pct<0||
           s.relative_humidity_pct>100) return false;
        RuuviSeen* slot=find(s.mac);
        if(!slot) {
            slot=free_slot();
            if(!slot) slot=oldest_slot();
            *slot=RuuviSeen{};
            slot->first_ms=now_ms;
            slot->used=true;
        }
        slot->latest=s;
        slot->last_ms=now_ms;
        if(slot->frames<0xFFFFFFFFu) ++slot->frames;
        if(counts_as_measurement(*slot,s,now_ms)) {
            slot->measured=s;
            slot->measured_ms=now_ms;
            slot->has_measurement=true;
        }
        return true;
    }
    // Copies up to `capacity` live entries, strongest signal first. A clock
    // that moved backwards makes the age unknown (entry skipped), not "new".
    size_t snapshot(uint64_t now_ms,RuuviSeen* out,size_t capacity) const {
        size_t n=0;
        for(const auto& e:slots_) {
            if(!e.used||n>=capacity) continue;
            if(now_ms<e.last_ms||now_ms-e.last_ms>kRuuviForgetMs) continue;
            out[n++]=e;
        }
        std::sort(out,out+n,[](const RuuviSeen& a,const RuuviSeen& b) {
            if(a.latest.rssi_dbm!=b.latest.rssi_dbm) return a.latest.rssi_dbm>b.latest.rssi_dbm;
            return a.latest.mac<b.latest.mac;
        });
        return n;
    }
    size_t used_count() const {
        size_t n=0;
        for(const auto& e:slots_) n+=e.used?1:0;
        return n;
    }
private:
    // Mirrors RuuviBinding::accept: repeated or older sequences are not new
    // measurements, and a restarted sequence is trusted only after the old
    // measurement is stale AND two consecutive advancing frames arrived.
    static bool counts_as_measurement(RuuviSeen& e,const RuuviV5& s,uint64_t ms) {
        if(!e.has_measurement) return true;
        if(ms<e.measured_ms) return false;
        const unsigned distance=ruuvi_sequence_distance(e.measured.sequence,s.sequence);
        if(distance==0) return false;
        if(distance>32767) {
            if(ms-e.measured_ms<=600000) return false;
            const unsigned step=ruuvi_sequence_distance(e.restart_sequence,s.sequence);
            if(!e.restart_candidate||step!=1||ms<=e.restart_ms||ms-e.restart_ms>120000) {
                e.restart_candidate=true;
                e.restart_sequence=s.sequence;
                e.restart_ms=ms;
                return false;
            }
            e.restart_candidate=false;
            return true;
        }
        e.restart_candidate=false;
        return true;
    }
    RuuviSeen* find(const std::array<uint8_t,6>& mac) {
        for(auto& e:slots_) if(e.used&&e.latest.mac==mac) return &e;
        return nullptr;
    }
    RuuviSeen* free_slot() {
        for(auto& e:slots_) if(!e.used) return &e;
        return nullptr;
    }
    RuuviSeen* oldest_slot() {
        RuuviSeen* oldest=&slots_[0];
        for(auto& e:slots_) if(e.last_ms<oldest->last_ms) oldest=&e;
        return oldest;
    }
    std::array<RuuviSeen,kRuuviRegistrySlots> slots_{};
};

// Display text from the owner: valid UTF-8, no controls, no markup characters.
inline bool safe_display_text(const char* s) {
    const unsigned char* p=reinterpret_cast<const unsigned char*>(s);
    while(*p) {
        const unsigned char c=*p;
        if(c<0x20||c==0x7f||c=='<'||c=='>'||c=='"'||c=='&'||c=='\\'||c=='\'') return false;
        size_t extra=0;
        if(c>=0xF0&&c<=0xF4) extra=3;
        else if(c>=0xE0) extra=2;
        else if(c>=0xC2) extra=1;
        else if(c>=0x80) return false;
        ++p;
        for(size_t i=0;i<extra;++i,++p) if((*p&0xC0)!=0x80) return false;
    }
    return true;
}

constexpr size_t kSensorNameBytes=24;              // incl. terminator
constexpr size_t kSensorNameSlots=8;
struct SensorNameEntry {
    std::array<uint8_t,6> mac{};
    char name[kSensorNameBytes]{};
    bool used=false;
};
enum class NameResult : uint8_t { Ok, Cleared, BadName, TableFull, BadMac };

// Names are labels only; they never decide which sensor is used.
class SensorNames {
public:
    NameResult set(const std::array<uint8_t,6>& mac,const char* name) {
        std::array<uint8_t,6> zero{},ff{};
        ff.fill(0xff);
        if(mac==zero||mac==ff) return NameResult::BadMac;
        if(!name) return NameResult::BadName;
        const size_t n=std::strlen(name);
        SensorNameEntry* e=find(mac);
        if(n==0) {
            if(e) *e=SensorNameEntry{};
            return NameResult::Cleared;
        }
        if(n>=kSensorNameBytes||!safe_display_text(name)) return NameResult::BadName;
        // A name made of spaces only is no name.
        bool visible=false;
        for(size_t i=0;i<n;++i) visible|=name[i]!=' ';
        if(!visible) return NameResult::BadName;
        if(!e) {
            for(auto& x:entries_) if(!x.used) {e=&x;break;}
            if(!e) return NameResult::TableFull;
            e->mac=mac;e->used=true;
        }
        std::memcpy(e->name,name,n+1);
        return NameResult::Ok;
    }
    const char* get(const std::array<uint8_t,6>& mac) const {
        for(const auto& e:entries_) if(e.used&&e.mac==mac) return e.name;
        return nullptr;
    }
    const std::array<SensorNameEntry,kSensorNameSlots>& entries() const {return entries_;}
    // Restores a persisted table; rejects (and returns false for) any entry
    // that would not pass set(), so a corrupt blob can never inject text.
    bool load(const std::array<SensorNameEntry,kSensorNameSlots>& raw) {
        SensorNames fresh;
        for(const auto& e:raw) {
            if(!e.used) continue;
            if(std::memchr(e.name,0,sizeof(e.name))==nullptr) return false;
            if(fresh.set(e.mac,e.name)!=NameResult::Ok) return false;
        }
        *this=fresh;
        return true;
    }
private:
    SensorNameEntry* find(const std::array<uint8_t,6>& mac) {
        for(auto& e:entries_) if(e.used&&e.mac==mac) return &e;
        return nullptr;
    }
    std::array<SensorNameEntry,kSensorNameSlots> entries_{};
};
} // namespace enevina::cottage
