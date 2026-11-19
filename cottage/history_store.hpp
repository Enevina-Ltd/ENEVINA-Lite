#pragma once
// Bounded, wear-aware history: 15-minute records for 30 days plus a short event
// log. Pure C++ (no flash, RTOS or HTTP): the firmware supplies memory, a clock
// and a file backend; everything here is host-tested.
//
// Principles
//  * A missing measurement stays missing (sentinel values). Nothing is
//    interpolated, carried forward or invented; a slot with no samples at all
//    produces no record, so outages show as gaps.
//  * Only a trusted clock records. A clock that moves backwards is ignored
//    until it passes the last finalized slot.
//  * Flash writes are bounded: records are appended in batches (the firmware
//    flushes hourly), the file is compacted rarely (after kHistoryCompactSlack
//    extra records), every record carries a CRC and a torn tail is cut at the
//    first bad record on load.
//  * Pump-side values keep their provenance flags (forecast outdoor value,
//    armed state) so the UI never presents a forecast as a measurement.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace enevina::cottage {
constexpr size_t kHistorySlots=2880;                 // 30 days of 15 min
constexpr int64_t kHistoryStepS=900;
constexpr size_t kHistoryCompactSlack=240;           // rewrite file after +2.5 days
constexpr size_t kEventSlots=128;

constexpr int16_t kMissingI16=std::numeric_limits<int16_t>::min();
constexpr uint16_t kMissingU16=0xFFFF;
constexpr int32_t kMissingI32=std::numeric_limits<int32_t>::min();

enum HistoryFlags : uint16_t {
    kActionMask=0x3,          // 0 unknown, 1 off, 2 heat, 3 hold (the controller's PROPOSAL)
    kOutdoorIsForecast=1<<2,  // outdoor value is the weather forecast, not a sensor
    kControlArmed=1<<3,       // automatic control was armed in this slot
    kPumpOnline=1<<4,
    kWifiOk=1<<5,
    kPowerScopeMask=0xC0      // bits 6-7: what the power meter measured in this slot
};
constexpr unsigned kPowerScopeShift=6;
// 0 unknown (owner has not confirmed), 1 heat pump only, 2 whole house. The scope
// is stored WITH each record: a later change of meter or of the owner's answer
// must never relabel readings that were taken under another one.

struct HistoryRecord {
    uint32_t slot=0;          // utc / 900
    int32_t price_e5=kMissingI32;       // total price per kWh x 1e5 (zone currency)
    int16_t indoor_c100=kMissingI16;
    int16_t outdoor_c100=kMissingI16;
    int16_t setpoint_c10=kMissingI16;   // pump-reported setpoint
    uint16_t power_w=kMissingU16;       // average watts of the metered load
    uint16_t rh_c100=kMissingU16;
    uint16_t flags=0;
};
static_assert(sizeof(HistoryRecord)==20,"history record must stay 20 bytes");

// ---- ring -------------------------------------------------------------------
// Slots are monotone: a record is accepted only when its slot is newer than the
// newest stored one. The ring keeps the last kHistorySlots slot numbers.
class HistoryStore {
public:
    // Owns its memory (tests, small builds) ...
    explicit HistoryStore(size_t capacity=kHistorySlots):cap_(capacity),owned_(capacity),data_(owned_.data()) {}
    // ... or uses caller-provided memory (the firmware places the ring in PSRAM).
    HistoryStore(HistoryRecord* external,size_t capacity):cap_(capacity),data_(external) {
        for(size_t i=0;i<capacity;++i) data_[i]=HistoryRecord{};
    }
    HistoryStore(const HistoryStore&)=delete;
    HistoryStore& operator=(const HistoryStore&)=delete;
    bool put(const HistoryRecord& r) {
        if(r.slot==0||(count_>0&&r.slot<=newest_)) return false;
        data_[r.slot%cap_]=r;
        newest_=r.slot;
        if(count_<cap_) ++count_;
        return true;
    }
    // Records still inside the ring window, otherwise nullptr (gap or evicted).
    const HistoryRecord* get(uint32_t slot) const {
        if(count_==0||slot>newest_||newest_-slot>=cap_) return nullptr;
        const HistoryRecord& r=data_[slot%cap_];
        return r.slot==slot?&r:nullptr;
    }
    uint32_t newest_slot() const {return newest_;}
    size_t capacity() const {return cap_;}
    size_t size() const {                       // records present in the window
        size_t n=0;
        for(size_t i=0;i<cap_;++i) {const auto& r=data_[i];if(r.slot!=0&&r.slot<=newest_&&newest_-r.slot<cap_) ++n;}
        return n;
    }
    bool empty() const {return count_==0;}
private:
    size_t cap_;
    std::vector<HistoryRecord> owned_;
    HistoryRecord* data_;
    uint32_t newest_=0;
    size_t count_=0;
};

// ---- events -----------------------------------------------------------------
enum class HistoryEventKind : uint8_t {
    Boot=1,            // value: reset reason code
    SourceLost=2,      // source says which one
    SourceBack=3,      // value: seconds it was missing (saturated)
    CommandSent=4,     // value: how many since the last check
    CommandConfirmed=5,
    CommandFailed=6
};
enum class HistorySource : uint8_t { None=0, Wifi=1, Pump=2, Sensor=3, Prices=4 };
struct HistoryEvent {
    uint32_t utc=0;
    uint8_t kind=0;
    uint8_t source=0;
    int16_t value=0;
};
static_assert(sizeof(HistoryEvent)==8,"event must stay 8 bytes");

class EventLog {
public:
    void put(const HistoryEvent& e) {
        ring_[next_]=e;
        next_=(next_+1)%kEventSlots;
        if(count_<kEventSlots) ++count_;
        ++total_;
    }
    size_t count() const {return count_;}
    uint64_t total() const {return total_;}
    // i = 0 is the newest event.
    const HistoryEvent& newest(size_t i) const {
        return ring_[(next_+kEventSlots-1-i)%kEventSlots];
    }
    // Restores from a persisted oldest->newest list.
    void restore(const HistoryEvent* events,size_t n) {
        *this=EventLog{};
        for(size_t i=n>kEventSlots?n-kEventSlots:0;i<n;++i) put(events[i]);
        total_=0;
    }
private:
    std::array<HistoryEvent,kEventSlots> ring_{};
    size_t next_=0,count_=0;
    uint64_t total_=0;
};

// ---- recorder ---------------------------------------------------------------
struct HistoryInputs {
    bool clock_trusted=false;
    int64_t utc=0;
    bool wifi_ok=false,pump_online=false,sensor_online=false,prices_ok=false;
    bool indoor_valid=false;   double indoor_c=0;
    bool rh_valid=false;       double rh_pct=0;
    bool outdoor_valid=false;  double outdoor_c=0;  bool outdoor_is_forecast=false;
    bool setpoint_valid=false; double setpoint_c=0;
    bool power_valid=false;    double power_w=0;  uint8_t power_scope=0;
    bool price_valid=false;    double price_per_kwh=0;
    uint8_t action=0;          // 0 unknown, 1 off, 2 heat, 3 hold
    bool armed=false;
    uint32_t commands_sent=0,commands_confirmed=0,commands_failed=0;   // monotone counters
    int reset_reason=0;
};

class HistoryRecorder {
public:
    static constexpr int64_t kDebounceS=60;      // a state must hold this long to become an event
    // Feed every ~10-15 s. Returns true when a record was finalized.
    bool observe(const HistoryInputs& in,HistoryStore& store,EventLog& events) {
        if(!in.clock_trusted||in.utc<=1700000000) return false;
        const uint32_t now_slot=static_cast<uint32_t>(in.utc/kHistoryStepS);
        bool finalized=false;
        if(!booted_) {
            booted_=true;
            events.put({static_cast<uint32_t>(in.utc),static_cast<uint8_t>(HistoryEventKind::Boot),0,
                        static_cast<int16_t>(in.reset_reason)});
            counters_known_=true;
            sent_=in.commands_sent;confirmed_=in.commands_confirmed;failed_=in.commands_failed;
        }
        track_source(in,HistorySource::Wifi,in.wifi_ok,events);
        track_source(in,HistorySource::Pump,in.pump_online,events);
        track_source(in,HistorySource::Sensor,in.sensor_online,events);
        track_source(in,HistorySource::Prices,in.prices_ok,events);
        command_events(in,events);
        if(acc_slot_!=0&&now_slot<acc_slot_) return false;          // clock went backwards: ignore
        if(acc_slot_!=0&&now_slot>acc_slot_) {
            finalized=finalize(store);
            reset(now_slot);
        } else if(acc_slot_==0) reset(now_slot);
        add(in);
        return finalized;
    }
    // On shutdown/flush time the open slot is NOT finalized: partial slots are
    // never stored as if complete.
private:
    struct Sum {double sum=0;uint32_t n=0;void add(double v){sum+=v;++n;}
                bool any()const{return n>0;} double avg()const{return sum/n;}};
    struct Track {bool state=false;bool known=false;int64_t since=0;bool reported=true;bool lost_reported=false;int64_t lost_at=0;};
    uint32_t acc_slot_=0;
    Sum indoor_,rh_,outdoor_,power_,price_;
    bool have_setpoint_=false;double setpoint_=0;
    uint8_t action_=0;bool armed_=false,outdoor_forecast_=false,pump_online_=false,wifi_ok_=false;
    uint8_t power_scope_=0;bool power_scope_set_=false,power_mixed_=false;
    uint32_t samples_=0;
    bool booted_=false,counters_known_=false;
    uint32_t sent_=0,confirmed_=0,failed_=0;
    Track tracks_[5];

    void reset(uint32_t slot) {
        acc_slot_=slot;indoor_=rh_=outdoor_=power_=price_=Sum{};
        have_setpoint_=false;action_=0;armed_=false;outdoor_forecast_=false;
        pump_online_=false;wifi_ok_=false;samples_=0;
        power_scope_=0;power_scope_set_=false;power_mixed_=false;
    }
    void add(const HistoryInputs& in) {
        ++samples_;
        if(in.indoor_valid&&std::isfinite(in.indoor_c)&&in.indoor_c>-40&&in.indoor_c<60) indoor_.add(in.indoor_c);
        if(in.rh_valid&&std::isfinite(in.rh_pct)&&in.rh_pct>=0&&in.rh_pct<=100) rh_.add(in.rh_pct);
        if(in.outdoor_valid&&std::isfinite(in.outdoor_c)&&in.outdoor_c>-60&&in.outdoor_c<60) {
            // Any forecast-derived sample makes the slot's average a forecast value:
            // the later measured sample must not clear the flag (it is an average
            // of both, so presenting it as a measurement would be false).
            outdoor_.add(in.outdoor_c);outdoor_forecast_=outdoor_forecast_||in.outdoor_is_forecast;
        }
        if(in.setpoint_valid&&std::isfinite(in.setpoint_c)&&in.setpoint_c>=0&&in.setpoint_c<=40) {
            have_setpoint_=true;setpoint_=in.setpoint_c;                  // last value of the slot
        }
        if(in.power_valid&&std::isfinite(in.power_w)&&in.power_w>=0&&in.power_w<65000) {
            const uint8_t scope=in.power_scope<=2?in.power_scope:0;
            if(!power_scope_set_) {power_scope_=scope;power_scope_set_=true;}
            else if(power_scope_!=scope) power_mixed_=true;          // meter changed inside the slot
            power_.add(in.power_w);
        }
        if(in.price_valid&&std::isfinite(in.price_per_kwh)&&std::fabs(in.price_per_kwh)<20000) price_.add(in.price_per_kwh);
        if(in.action<=3) action_=in.action;
        armed_=armed_||in.armed;
        pump_online_=pump_online_||in.pump_online;
        wifi_ok_=wifi_ok_||in.wifi_ok;
    }
    bool finalize(HistoryStore& store) {
        if(samples_==0||acc_slot_==0) return false;
        HistoryRecord r;
        r.slot=acc_slot_;
        if(indoor_.any()) r.indoor_c100=static_cast<int16_t>(std::lround(indoor_.avg()*100));
        if(rh_.any()) r.rh_c100=static_cast<uint16_t>(std::lround(rh_.avg()*100));
        if(outdoor_.any()) r.outdoor_c100=static_cast<int16_t>(std::lround(outdoor_.avg()*100));
        if(have_setpoint_) r.setpoint_c10=static_cast<int16_t>(std::lround(setpoint_*10));
        // Two different measurement targets in one slot cannot be averaged into one
        // honest number: the power stays missing for that slot.
        if(power_.any()&&!power_mixed_) {
            r.power_w=static_cast<uint16_t>(std::min(65000.0,std::lround(power_.avg())*1.0));
        }
        if(price_.any()) r.price_e5=static_cast<int32_t>(std::lround(price_.avg()*1e5));
        r.flags=static_cast<uint16_t>(action_&kActionMask)|(outdoor_forecast_?kOutdoorIsForecast:0)|
                (armed_?kControlArmed:0)|(pump_online_?kPumpOnline:0)|(wifi_ok_?kWifiOk:0);
        if(r.power_w!=kMissingU16) r.flags|=static_cast<uint16_t>(power_scope_<<kPowerScopeShift);
        return store.put(r);
    }
    void track_source(const HistoryInputs& in,HistorySource src,bool ok,EventLog& events) {
        Track& t=tracks_[static_cast<size_t>(src)];
        if(!t.known) {t.known=true;t.state=ok;t.since=in.utc;t.reported=ok;return;}
        if(ok!=t.state) {t.state=ok;t.since=in.utc;}
        // Event only after the new state has held for kDebounceS (no flapping spam).
        if(t.state!=t.reported&&in.utc-t.since>=kDebounceS) {
            if(!t.state) {
                events.put({static_cast<uint32_t>(t.since),static_cast<uint8_t>(HistoryEventKind::SourceLost),
                            static_cast<uint8_t>(src),0});
                t.lost_reported=true;t.lost_at=t.since;
            } else {
                const int64_t down=in.utc-(t.lost_reported?t.lost_at:t.since);
                events.put({static_cast<uint32_t>(t.since),static_cast<uint8_t>(HistoryEventKind::SourceBack),
                            static_cast<uint8_t>(src),static_cast<int16_t>(std::min<int64_t>(down,32000))});
                t.lost_reported=false;
            }
            t.reported=t.state;
        }
    }
    void command_events(const HistoryInputs& in,EventLog& events) {
        auto emit=[&](HistoryEventKind k,uint32_t now,uint32_t& before) {
            if(now>before) {
                events.put({static_cast<uint32_t>(in.utc),static_cast<uint8_t>(k),0,
                            static_cast<int16_t>(std::min<uint32_t>(now-before,32000))});
            }
            before=now;                      // a counter that went down (reboot) just re-bases
        };
        emit(HistoryEventKind::CommandSent,in.commands_sent,sent_);
        emit(HistoryEventKind::CommandConfirmed,in.commands_confirmed,confirmed_);
        emit(HistoryEventKind::CommandFailed,in.commands_failed,failed_);
    }
};
} // namespace enevina::cottage
