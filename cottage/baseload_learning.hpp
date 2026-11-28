#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include "pump/pump_adapter.hpp"
#include "cottage/telemetry.hpp"
#include "cottage/history_persist.hpp"

namespace enevina::cottage {

// This is an estimate of the meter's total OFF load, including pump
// standby. It is NOT a measurement of delivered thermal power or COP.
// Constant indoor heat is already identified from room response by the
// thermal model; adding these watts to its learned gain double-counts it.
struct BaseloadEstimate {
    uint32_t identity=0, revision=0;
    int64_t observed_utc=0;
    double watts=0, spread_w=0;
    unsigned samples=0;
    bool ready=false;
};
inline double baseload_tolerance_w(double watts) {
    return std::max(15.0,0.10*watts);
}
inline bool baseload_current(const BaseloadEstimate& b,uint32_t identity,int64_t now) {
    return b.ready && identity && b.identity==identity && b.observed_utc>0 &&
        now>=b.observed_utc && now-b.observed_utc<=2*3600;
}

// Durable qualified OFF regime. Never serialize an incomplete sample window
// or carry request generations across a firmware reboot.
struct BaseloadCheckpoint {
    uint32_t schema=1,model_key=0,meter=0,revision=0;
    int64_t observed_utc=0;
    double watts=0,spread_w=0,counter_wh=0;
    uint16_t crc=0;
    uint8_t reserved[6]{};
};
static_assert(offsetof(BaseloadCheckpoint,crc)==48 && sizeof(BaseloadCheckpoint)==56,
              "baseload checkpoint layout");
enum class BaseloadRestore {Pending,Rejected,Restored};
// Bounded, allocation-free robust median of seven independent samples
// within an uninterrupted OFF request. Ignore the first four minutes of
// OFF (fan overrun/afterheat), defrost, stale reads, replay and source
// changes. A single deviation invalidates the old estimate for electrical
// subtraction until a new stable window is observed. Never use a minimum
// over arbitrary HEAT intervals as a learned baseline.
class BaseloadLearner {
public:
    static constexpr unsigned required_samples=7;
    BaseloadEstimate observe(uint32_t identity,int64_t now,
            const enevina::PumpRoomState& request,const ShellyTelemetry& meter,
            bool relay_on,bool defrost) {
        if(identity!=estimate_.identity) {
            reset_window();
            estimate_={};
            estimate_.identity=identity;
            last_meter_utc_=0;
            last_request_utc_=0;
            last_total_wh_=0;
        }
        if(!relay_on && meter.received && meter.source_identity==identity &&
           valid_shelly(meter,now,30)) {
            if(estimate_.ready)++estimate_.revision;
            estimate_.ready=false;reset_window();
        }
        if(!identity || !relay_on || defrost || !meter.received ||
           meter.source_identity!=identity || !valid_shelly(meter,now,30) ||
           !std::isfinite(meter.watts) || meter.watts<0 || meter.watts>20000 ||
           !std::isfinite(meter.total_wh) || meter.total_wh<0 ||
           request.source==enevina::PumpStateSource::None ||
           (request.source==enevina::PumpStateSource::Gateway &&
            request.protocol!=enevina::PumpRoomProtocol::Cn105EspHome) ||
           (request.source==enevina::PumpStateSource::VerifiedBus &&
            !enevina::verified_fresh(request.evidence,now,30)) ||
           request.command_unconfirmed || request.write_in_progress ||
           request.evidence.received_utc<=0 ||
           now<request.evidence.received_utc || now-request.evidence.received_utc>30) {
            reset_window();
            return snapshot(identity,now);
        }
        // Counter reset, clock reversal or source replay cannot continue
        // an OFF window. A reset also withdraws the last estimate.
        if((last_meter_utc_ && (meter.captured_utc<last_meter_utc_ ||
            meter.total_wh<last_total_wh_)) ||
           (last_request_utc_ && request.evidence.received_utc<last_request_utc_)) {
            reset_window();
            estimate_.ready=false;
            ++estimate_.revision;
            last_meter_utc_=last_request_utc_=0;
            estimate_.observed_utc=0;
        }
        if(request.mode!=enevina::PumpRoomMode::Off ||
           request.mode_minutes<4 || request.request_minutes<4) {
            reset_window();
            return snapshot(identity,now);
        }
        // The gateway request is not physical compressor provenance.
        // Require paired fresh meter evidence, observed quiet request and
        // stable low power; a residual standby load remains in the median.
        if(std::llabs(meter.captured_utc-request.evidence.received_utc)>30) {
            reset_window();return snapshot(identity,now);
        }
        if(meter.captured_utc<=last_meter_utc_ ||
           request.evidence.received_utc<=last_request_utc_)
            return snapshot(identity,now);
        const bool same=have_window_ &&
            request.protocol==protocol_ && request.source==source_ &&
            request.request_generation==request_generation_ &&
            request.endpoint_generation==endpoint_ &&
            request.link_generation==link_ &&
            request.configuration_generation==configuration_ &&
            request.write_generation==write_generation_ &&
            meter.captured_utc-last_meter_utc_<=150;
        if(!same)reset_window();
        have_window_=true;
        protocol_=request.protocol;source_=request.source;
        request_generation_=request.request_generation;
        endpoint_=request.endpoint_generation;link_=request.link_generation;
        configuration_=request.configuration_generation;
        write_generation_=request.write_generation;
        last_meter_utc_=meter.captured_utc;last_total_wh_=meter.total_wh;
        last_request_utc_=request.evidence.received_utc;
        if(estimate_.ready &&
           std::fabs(meter.watts-estimate_.watts)>baseload_tolerance_w(estimate_.watts)) {
            estimate_.ready=false;
            ++estimate_.revision;
            count_=head_=0;
        }
        // The seven samples must cover five minutes even if polling is
        // faster than one minute (e.g. near a quarter-hour boundary).
        // Inspect ALL fresh readings for changes, including those not
        // retained in the downsampled qualification window.
        if(count_ && meter.captured_utc-last_window_utc_<50) {
            const double previous=window_[(head_+required_samples-1)%required_samples].watts;
            if(std::fabs(meter.watts-previous)<=baseload_tolerance_w(previous))
                return snapshot(identity,now);
            count_=head_=0; // a fast intervening load change restarts qualification
        }
        window_[head_]={meter.captured_utc,meter.watts};
        last_window_utc_=meter.captured_utc;
        head_=(head_+1)%required_samples;
        if(count_<required_samples)++count_;
        if(count_==required_samples) {
            std::array<double,required_samples> values{};
            int64_t oldest=meter.captured_utc;
            for(unsigned i=0;i<required_samples;++i) {
                values[i]=window_[i].watts;
                oldest=std::min(oldest,window_[i].utc);
            }
            std::sort(values.begin(),values.end());
            const double median=values[required_samples/2];
            const double spread=values.back()-values.front();
            if(meter.captured_utc-oldest>=300 &&
               spread<=baseload_tolerance_w(median)) {
                // Freeze the subtraction reference for a regime; small
                // jitter must not invalidate every 15-minute energy slot.
                if(!estimate_.ready) {
                    estimate_.watts=median;
                    ++estimate_.revision;
                }
                estimate_.observed_utc=meter.captured_utc;
                estimate_.spread_w=spread;
                estimate_.samples=required_samples;
                estimate_.ready=true;
            }
        }
        return snapshot(identity,now);
    }
    void checkpoint(uint32_t model_key,int64_t now,BaseloadCheckpoint& out) const {
        out=BaseloadCheckpoint{};out.model_key=model_key;
        if(model_key && baseload_current(estimate_,estimate_.identity,now)) {
            out.meter=estimate_.identity;out.revision=estimate_.revision;
            out.observed_utc=estimate_.observed_utc;out.watts=estimate_.watts;
            out.spread_w=estimate_.spread_w;out.counter_wh=last_total_wh_;
        }
        out.crc=history_crc16(reinterpret_cast<const uint8_t*>(&out),offsetof(BaseloadCheckpoint,crc));
    }
    // Only after observe() qualified a new fresh OFF reading from the same
    // meter and counter. A stored estimate is NOT a fresh observation.
    BaseloadRestore restore_checkpoint(const BaseloadCheckpoint& saved,uint32_t model_key,
                                       uint32_t meter,int64_t now) {
        if(saved.schema!=1 || !model_key || !meter || saved.model_key!=model_key ||
           saved.meter!=meter || saved.crc!=history_crc16(
               reinterpret_cast<const uint8_t*>(&saved),offsetof(BaseloadCheckpoint,crc)) ||
           !saved.revision || saved.observed_utc<=0 || now<saved.observed_utc ||
           now-saved.observed_utc>7200 || !std::isfinite(saved.watts) || saved.watts<0 ||
           saved.watts>20000 || !std::isfinite(saved.spread_w) || saved.spread_w<0 ||
           saved.spread_w>baseload_tolerance_w(saved.watts) ||
           !std::isfinite(saved.counter_wh) || saved.counter_wh<0 ||
           std::any_of(std::begin(saved.reserved),std::end(saved.reserved),[](uint8_t x){return x!=0;}))
            return BaseloadRestore::Rejected;
        if(baseload_current(estimate_,meter,now))return BaseloadRestore::Rejected;
        if(estimate_.identity!=meter || !have_window_ || !count_ ||
           last_meter_utc_<=saved.observed_utc)return BaseloadRestore::Pending;
        const auto& newest=window_[(head_+required_samples-1)%required_samples];
        if(last_total_wh_<saved.counter_wh || now-newest.utc>30 ||
           std::fabs(newest.watts-saved.watts)>baseload_tolerance_w(saved.watts))
            return BaseloadRestore::Rejected;
        estimate_={meter,saved.revision,last_meter_utc_,saved.watts,saved.spread_w,
                   required_samples,true};
        return BaseloadRestore::Restored;
    }
    BaseloadEstimate snapshot(uint32_t identity,int64_t now) const {
        auto e=estimate_;
        e.ready=baseload_current(e,identity,now);
        if(!e.ready)e.samples=count_;
        return e;
    }
private:
    struct Point {int64_t utc=0;double watts=0;};
    void reset_window(){have_window_=false;count_=head_=0;last_window_utc_=0;}
    std::array<Point,required_samples> window_{};
    BaseloadEstimate estimate_{};
    unsigned count_=0,head_=0;
    bool have_window_=false;
    int64_t last_meter_utc_=0,last_request_utc_=0,last_window_utc_=0;
    double last_total_wh_=0;
    enevina::PumpRoomProtocol protocol_=enevina::PumpRoomProtocol::Unknown;
    enevina::PumpStateSource source_=enevina::PumpStateSource::None;
    uint32_t request_generation_=0,endpoint_=0,link_=0,configuration_=0,write_generation_=0;
};

// Counter-based increment above the verified OFF load. Never label it
// pump-only or true COP. Both slot ends must agree on the same qualified
// baseline and meter; the caller must separately verify the heating request.
inline bool incremental_slot_energy(const BaseloadEstimate& begin,
        const BaseloadEstimate& end,uint32_t identity,int64_t start_utc,int64_t end_utc,
        double start_wh,double end_wh,bool contains_heat,double& kwh) {
    if(!baseload_current(begin,identity,start_utc) ||
       !baseload_current(end,identity,end_utc) || begin.revision!=end.revision ||
       begin.watts!=end.watts || !std::isfinite(start_wh) || !std::isfinite(end_wh) ||
       end_wh<start_wh || end_utc-start_utc<840 || end_utc-start_utc>960)return false;
    const double watts=(end_wh-start_wh)*3600.0/double(end_utc-start_utc);
    const double net=watts-end.watts;
    const double tol=baseload_tolerance_w(end.watts);
    if(!std::isfinite(net) || net < -tol || (!contains_heat && std::fabs(net)>tol) ||
       net>20000)return false;
    kwh=std::max(0.0,net)*0.25/1000.0;
    return true;
}
} // namespace enevina::cottage
