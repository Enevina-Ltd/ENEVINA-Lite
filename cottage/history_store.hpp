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
    kPowerScopeMask=0xC0,     // bits 6-7: metered target
    kPriceCurrencyMask=0x0700,// bits 8-10: immutable observation currency (0=old/unknown)
    kPowerCoverageMask=0x7800,// bits 11-14: verified whole minutes out of 15
    kOutdoorMixed=0x8000      // both forecast and measured outdoors in this slot
};
constexpr unsigned kPowerScopeShift=6;
constexpr unsigned kPriceCurrencyShift=8;
constexpr unsigned kPowerCoverageShift=11;
// Keep the 20-byte persisted record and its CRC/format fully backward-readable.
// Unannotated legacy records have UNKNOWN currency and ZERO verified coverage:
// no earlier sample may be retroactively treated as a full 15-minute reading.
inline uint8_t history_currency_id(const char* currency) {
    if(!currency)return 0;
    if(!std::strcmp(currency,"EUR"))return 1;
    if(!std::strcmp(currency,"SEK"))return 2;
    if(!std::strcmp(currency,"NOK"))return 3;
    if(!std::strcmp(currency,"DKK"))return 4;
    if(!std::strcmp(currency,"PLN"))return 5;
    return 0;
}
inline const char* history_currency_name(unsigned code) {
    switch(code) {
    case 1:return "EUR";case 2:return "SEK";case 3:return "NOK";
    case 4:return "DKK";case 5:return "PLN";case 7:return "mixed";
    default:return "unknown";
    }
}
inline uint8_t history_power_coverage_minutes(uint16_t flags) {
    return static_cast<uint8_t>((flags&kPowerCoverageMask)>>kPowerCoverageShift);
}
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
    CommandFailed=6,
    UpdateConfirmed=7, // arg: the signed release version that was confirmed good
    RoomTemperatureSourceChanged=8 // arg: 0 Ruuvi, 1 pump. value: correction in hundredths of a degree.
};
enum class HistorySource : uint8_t { None=0, Wifi=1, Pump=2, Sensor=3, Prices=4 };
struct HistoryEvent {
    uint32_t utc=0;
    uint8_t kind=0;
    uint8_t source=0;
    int16_t value=0;
    uint32_t arg=0;           // kind-specific 32-bit detail (e.g. release version)
};
static_assert(sizeof(HistoryEvent)==12,"event must stay 12 bytes");

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
struct HistoryCn105Ack {
    uint32_t utc=0;
    uint8_t step=0;   // 1 setpoint, 2 HEAT, 3 OFF
    uint8_t source=0; // 1 MPC, 2 price control, 3 thermostat
    uint16_t target_c10=0;
};

struct HistoryInputs {
    bool clock_trusted=false;
    int64_t utc=0;
    bool wifi_ok=false,pump_online=false,sensor_online=false,prices_ok=false;
    bool indoor_valid=false;   double indoor_c=0;
    bool indoor_from_pump=false;
    int16_t indoor_correction_c100=0;
    uint64_t indoor_source_id=0;
    bool rh_valid=false;       double rh_pct=0;
    bool outdoor_valid=false;  double outdoor_c=0;  bool outdoor_is_forecast=false;
    bool setpoint_valid=false; double setpoint_c=0;
    bool power_valid=false;    double power_w=0;  uint8_t power_scope=0;
    int64_t power_sample_utc=0; // Shelly capture time, not the 15-second history poll
    bool price_valid=false;    double price_per_kwh=0; uint8_t price_currency=0;
    uint8_t action=0;          // 0 unknown, 1 off, 2 heat, 3 hold
    bool armed=false;
    uint32_t commands_sent=0,commands_confirmed=0,commands_failed=0;   // monotone counters
    HistoryCn105Ack cn105_ack{};
    int reset_reason=0;
    uint32_t ota_good_version=0;   // last version the signed OTA confirmed good (0 = unknown)
    uint32_t ota_upgrade_event_version=0; // signed OTA proof, not just a new observed number
};

class HistoryRecorder {
public:
    static constexpr int64_t kDebounceS=60;      // a state must hold this long to become an event
    // Feed every ~10-15 s. Returns true when a record was finalized.
    bool observe(const HistoryInputs& in,HistoryStore& store,EventLog& events) {
        // OTA boot confirmation may precede NTP. Remember the previously
        // observed confirmed version even while event timestamps are unknown.
        track_ota_version(in.ota_good_version,in.ota_upgrade_event_version,events);
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
        // The clock may have become trusted after OTA confirmation. The
        // event is recorded at the first trusted observation, never with an
        // invented timestamp from the clock-untrusted period.
        if(ota_pending_version_!=0) {
            events.put({static_cast<uint32_t>(in.utc),static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed),0,0,
                        ota_pending_version_});
            ota_pending_version_=0;
        }
        track_source(in,HistorySource::Wifi,in.wifi_ok,events);
        track_source(in,HistorySource::Pump,in.pump_online,events);
        track_source(in,HistorySource::Sensor,in.sensor_online,events);
        track_source(in,HistorySource::Prices,in.prices_ok,events);
        command_events(in,events);
        if(acc_slot_!=0&&now_slot<acc_slot_) return false;          // clock went backwards: ignore
        const bool indoor_changed=indoor_source_known_ &&
            (in.indoor_from_pump!=indoor_from_pump_ ||
             in.indoor_correction_c100!=indoor_correction_c100_ ||
             in.indoor_source_id!=indoor_source_id_);
        const bool same_slot=now_slot==acc_slot_;
        if(acc_slot_!=0&&now_slot>acc_slot_) {
            finalized=finalize(store);
            reset(now_slot);
        } else if(acc_slot_==0) reset(now_slot);
        if(indoor_changed || (!indoor_source_known_ &&
                             (in.indoor_from_pump || in.indoor_correction_c100!=0))) {
            events.put({static_cast<uint32_t>(in.utc),
                static_cast<uint8_t>(HistoryEventKind::RoomTemperatureSourceChanged),
                static_cast<uint8_t>(HistorySource::Sensor),in.indoor_correction_c100,
                static_cast<uint32_t>(in.indoor_from_pump)});
        }
        indoor_source_known_=true;
        indoor_source_id_=in.indoor_source_id;
        indoor_correction_c100_=in.indoor_correction_c100;
        indoor_from_pump_=in.indoor_from_pump;
        if(same_slot && indoor_changed)indoor_mixed_=true;
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
    bool indoor_source_known_=false,indoor_mixed_=false,indoor_from_pump_=false;
    int16_t indoor_correction_c100_=0;
    uint64_t indoor_source_id_=0;
    bool have_setpoint_=false;double setpoint_=0;
    uint8_t action_=0;bool armed_=false,outdoor_forecast_=false,outdoor_measured_=false;
    bool pump_online_=false,wifi_ok_=false;
    uint8_t power_scope_=0;bool power_scope_set_=false,power_mixed_=false;
    uint8_t price_currency_=0;bool price_currency_seen_=false,price_mixed_=false;
    // Trapezoid integral ONLY between two consecutive, valid samples. A
    // single 1000 W report at 14:50 never claims 0.25 kWh for that slot.
    double verified_watt_seconds_=0,last_power_w_=0;
    uint32_t verified_power_seconds_=0;
    int64_t last_power_sample_utc_=0;
    uint8_t last_power_scope_=0;
    bool last_power_valid_=false;
    uint32_t samples_=0;
    bool booted_=false,counters_known_=false;
    uint32_t sent_=0,confirmed_=0,failed_=0;
    uint32_t ota_good_=0,ota_pending_version_=0;
    Track tracks_[5];

    void track_ota_version(uint32_t version,uint32_t confirmed_upgrade,
                           const EventLog& events) {
        if(version==0)return; // OTA store has not loaded
        // A confirmed upgrade can already be the FIRST version observed by
        // this in-memory recorder after boot. The persisted signed-OTA hint
        // proves it was a transition, not just a factory first boot. Dedup
        // against existing flash history on reboot while the hint remains.
        const bool explicitly_confirmed=confirmed_upgrade!=0 &&
            confirmed_upgrade==version;
        const bool observed_transition=ota_good_!=0 && version>ota_good_;
        bool already_logged=false;
        for(size_t i=0;i<events.count();++i) {
            const auto& e=events.newest(i);
            if(e.kind==static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed) &&
               e.arg==version) {already_logged=true;break;}
        }
        if((explicitly_confirmed||observed_transition)&&!already_logged)
            ota_pending_version_=version;
        else if(already_logged && ota_pending_version_==version)
            ota_pending_version_=0;
        if(ota_good_!=0 && version<ota_good_)
            ota_pending_version_=0; // rollback, not success
        ota_good_=version;
    }

    void reset(uint32_t slot) {
        acc_slot_=slot;indoor_=rh_=outdoor_=power_=price_=Sum{};
        indoor_mixed_=false;
        have_setpoint_=false;action_=0;armed_=false;outdoor_forecast_=false;
        outdoor_measured_=false;pump_online_=false;wifi_ok_=false;samples_=0;
        power_scope_=0;power_scope_set_=false;power_mixed_=false;
        price_currency_=0;price_currency_seen_=false;price_mixed_=false;
        verified_watt_seconds_=0;verified_power_seconds_=0;
        last_power_w_=0;last_power_sample_utc_=0;last_power_scope_=0;last_power_valid_=false;
    }
    void add(const HistoryInputs& in) {
        ++samples_;
        if(in.indoor_valid&&std::isfinite(in.indoor_c)&&in.indoor_c>-40&&in.indoor_c<60) indoor_.add(in.indoor_c);
        if(in.rh_valid&&std::isfinite(in.rh_pct)&&in.rh_pct>=0&&in.rh_pct<=100) rh_.add(in.rh_pct);
        if(in.outdoor_valid&&std::isfinite(in.outdoor_c)&&in.outdoor_c>-60&&in.outdoor_c<60) {
            // Any forecast-derived sample makes the slot's average a forecast value:
            // the later measured sample must not clear the flag (it is an average
            // of both, so presenting it as a measurement would be false).
            outdoor_.add(in.outdoor_c);
            if(in.outdoor_is_forecast) outdoor_forecast_=true;
            else outdoor_measured_=true;
        }
        if(in.setpoint_valid&&std::isfinite(in.setpoint_c)&&in.setpoint_c>=0&&in.setpoint_c<=40) {
            have_setpoint_=true;setpoint_=in.setpoint_c;                  // last value of the slot
        }
        const int64_t capture=in.power_sample_utc>0?in.power_sample_utc:in.utc;
        if(in.power_valid && capture>0 && capture<=in.utc &&
           capture/kHistoryStepS==acc_slot_ &&
           std::isfinite(in.power_w) && in.power_w>=0 && in.power_w<65000) {
            const uint8_t scope=in.power_scope<=2?in.power_scope:0;
            // Re-reading the SAME Shelly capture from RAM is not another
            // physical observation, even if the HTTP history loop ran again.
            if(!last_power_valid_ || capture>last_power_sample_utc_) {
                if(!power_scope_set_) {power_scope_=scope;power_scope_set_=true;}
                else if(power_scope_!=scope) power_mixed_=true;
                if(last_power_valid_ && last_power_scope_==scope &&
                   capture-last_power_sample_utc_<=180) {
                    const uint32_t seconds=static_cast<uint32_t>(capture-last_power_sample_utc_);
                    verified_watt_seconds_+=(last_power_w_+in.power_w)*0.5*seconds;
                    verified_power_seconds_+=seconds;
                }
                last_power_valid_=true;last_power_w_=in.power_w;
                last_power_scope_=scope;last_power_sample_utc_=capture;
                power_.add(in.power_w);
            }
        } else {
            // Never bridge a stale Shelly reading into a different slot.
            last_power_valid_=false;
        }
        if(in.price_valid&&std::isfinite(in.price_per_kwh)&&std::fabs(in.price_per_kwh)<20000) {
            const uint8_t code=in.price_currency<=5?in.price_currency:0;
            if(!price_currency_seen_) {price_currency_=code;price_currency_seen_=true;}
            else if(price_currency_!=code) price_mixed_=true;
            price_.add(in.price_per_kwh);
        }
        if(in.action<=3) action_=in.action;
        armed_=armed_||in.armed;
        pump_online_=pump_online_||in.pump_online;
        wifi_ok_=wifi_ok_||in.wifi_ok;
    }
    bool finalize(HistoryStore& store) {
        if(samples_==0||acc_slot_==0) return false;
        HistoryRecord r;
        r.slot=acc_slot_;
        if(indoor_.any() && !indoor_mixed_) r.indoor_c100=static_cast<int16_t>(std::lround(indoor_.avg()*100));
        if(rh_.any()) r.rh_c100=static_cast<uint16_t>(std::lround(rh_.avg()*100));
        if(outdoor_.any()) r.outdoor_c100=static_cast<int16_t>(std::lround(outdoor_.avg()*100));
        if(have_setpoint_) r.setpoint_c10=static_cast<int16_t>(std::lround(setpoint_*10));
        // Two different measurement targets in one slot cannot be averaged into one
        // honest number: the power stays missing for that slot.
        if(power_.any()&&!power_mixed_) {
            const double verified_average=verified_power_seconds_?
                verified_watt_seconds_/verified_power_seconds_:power_.avg();
            r.power_w=static_cast<uint16_t>(std::min(65000.0,std::lround(verified_average)*1.0));
        }
        if(price_.any()&&!price_mixed_)
            r.price_e5=static_cast<int32_t>(std::lround(price_.avg()*1e5));
        r.flags=static_cast<uint16_t>(action_&kActionMask)|(outdoor_forecast_?kOutdoorIsForecast:0)|
                (outdoor_forecast_&&outdoor_measured_?kOutdoorMixed:0)|
                (armed_?kControlArmed:0)|(pump_online_?kPumpOnline:0)|(wifi_ok_?kWifiOk:0);
        if(r.power_w!=kMissingU16) {
            r.flags|=static_cast<uint16_t>(power_scope_<<kPowerScopeShift);
            const unsigned verified_minutes=std::min<uint32_t>(15,verified_power_seconds_/60);
            r.flags|=static_cast<uint16_t>(verified_minutes<<kPowerCoverageShift);
        }
        if(r.price_e5!=kMissingI32)
            r.flags|=static_cast<uint16_t>(price_currency_<<kPriceCurrencyShift);
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
                // Confirm recovery after debounce, but measure downtime through
                // the FIRST healthy observation, not the later debounce tick.
                const int64_t down=t.since-(t.lost_reported?t.lost_at:t.since);
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
                HistoryEvent event{static_cast<uint32_t>(in.utc),static_cast<uint8_t>(k),0,
                                   static_cast<int16_t>(std::min<uint32_t>(now-before,32000))};
                const auto& ack=in.cn105_ack;
                // We know the details of the last confirmation only.
                if(k==HistoryEventKind::CommandConfirmed && now-before==1 &&
                   ack.utc>0 && ack.utc<=in.utc && ack.step>=1 && ack.step<=3 &&
                   ack.source<=3 && (ack.step==3?ack.target_c10==0:
                     (ack.target_c10>=160 && ack.target_c10<=260))) {
                    event.utc=ack.utc;
                    event.source=static_cast<uint8_t>(HistorySource::Pump);
                    event.arg=0x80000000u | ack.step | (uint32_t(ack.source)<<3) |
                              (uint32_t(ack.target_c10)<<8);
                }
                events.put(event);
            }
            before=now;                      // a counter that went down (reboot) just re-bases
        };
        emit(HistoryEventKind::CommandSent,in.commands_sent,sent_);
        emit(HistoryEventKind::CommandConfirmed,in.commands_confirmed,confirmed_);
        emit(HistoryEventKind::CommandFailed,in.commands_failed,failed_);
    }
};
} // namespace enevina::cottage
