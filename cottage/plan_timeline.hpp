#pragma once
// Plan timeline: what the live price controller will treat as cheap, neutral
// or expensive over the published price horizon, and the room-temperature
// headroom its profile may use in those periods. Pure C++, host-tested; the
// firmware only streams the JSON this header produces.
//
// What this is NOT: a predicted indoor temperature or a cost estimate. Those
// need a calibrated building model, which does not exist yet; the timeline
// never invents them. It also does not claim that the weather forecast shapes
// the decision - the live controller does not use outdoor temperature.
//
// Every slot is classified with cottage/price_bootstrap.hpp::classify_price,
// the same function the controller calls, over the same window rule (profile
// window, shortened near the end of the data, at least 8 slots).
#include "cottage/optimization_profile.hpp"
#include "cottage/price_bootstrap.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace enevina::cottage {
constexpr size_t kMaxTimelineSlots=192;
constexpr size_t kMaxTimelineSegments=32;

struct TimelineSlot {
    int64_t utc=0;
    double total=0;            // currency per kWh, all components
    PriceClass cls=PriceClass::Unknown;
    double median=0;           // median of this slot's comparison window
};
struct TimelineSegment {
    int64_t start=0;           // UTC of the first slot
    int64_t end=0;             // UTC end of the last slot
    PriceClass cls=PriceClass::Unknown;
    size_t slots=0;
    double avg_total=0;
    double avg_median=0;
    double min_total=0,max_total=0;
};
struct Timeline {
    size_t slot_count=0;
    TimelineSlot slots[kMaxTimelineSlots];
    size_t segment_count=0;
    TimelineSegment segments[kMaxTimelineSegments];
    bool truncated_at_gap=false;     // data after a missing slot is not shown
    bool segments_truncated=false;
};
struct TimelineParams {
    size_t window_slots=8;           // profile window
    double advantage_fraction=0.10;  // already scaled by the profile
    double advantage_eur_floor=0.005;
    double price_limit_scale=1.0; // selected currency's ratio to EUR-unit bounds
    size_t min_segment_slots=2;      // a single odd slot is not a "period"
};

// utc/total are parallel arrays of 15-minute slots starting at a quarter hour.
// Returns false when nothing usable is left (no slots, bad alignment).
inline bool build_timeline(const int64_t* utc,const double* total,size_t count,
                           const TimelineParams& params,Timeline& out) {
    out=Timeline{};
    if(!utc||!total||count==0||params.window_slots<8||params.window_slots>96 ||
       !std::isfinite(params.price_limit_scale) || params.price_limit_scale<1 ||
       params.price_limit_scale>12) return false;
    if(utc[0]<=0||utc[0]%900!=0) return false;
    size_t n=0;
    for(;n<count&&n<kMaxTimelineSlots;++n) {
        if(utc[n]!=utc[0]+static_cast<int64_t>(n)*900) {out.truncated_at_gap=true;break;}
        if(!std::isfinite(total[n]) ||
           total[n]< -params.price_limit_scale ||
           total[n]>10*params.price_limit_scale) {out.truncated_at_gap=true;break;}
    }
    if(n==0) return false;
    // `total` window slices are contiguous because the prefix is verified above.
    for(size_t i=0;i<n;++i) {
        TimelineSlot& s=out.slots[i];
        s.utc=utc[i];
        s.total=total[i];
        const size_t w=std::min(params.window_slots,n-i);
        const PriceClassResult c=classify_price(total+i,w,params.advantage_fraction,
                                                params.advantage_eur_floor,params.price_limit_scale);
        s.cls=c.cls;
        s.median=c.median;
    }
    out.slot_count=n;
    // Segments: runs of the same Cheap/Expensive class.
    size_t i=0;
    while(i<n) {
        const PriceClass cls=out.slots[i].cls;
        size_t j=i;
        while(j<n&&out.slots[j].cls==cls) ++j;
        if((cls==PriceClass::Cheap||cls==PriceClass::Expensive)&&j-i>=params.min_segment_slots) {
            if(out.segment_count>=kMaxTimelineSegments) {out.segments_truncated=true;break;}
            TimelineSegment& g=out.segments[out.segment_count++];
            g.start=out.slots[i].utc;
            g.end=out.slots[j-1].utc+900;
            g.cls=cls;
            g.slots=j-i;
            g.min_total=g.max_total=out.slots[i].total;
            double sum=0,msum=0;
            for(size_t k=i;k<j;++k) {
                sum+=out.slots[k].total;
                msum+=out.slots[k].median;
                if(out.slots[k].total<g.min_total) g.min_total=out.slots[k].total;
                if(out.slots[k].total>g.max_total) g.max_total=out.slots[k].total;
            }
            g.avg_total=sum/static_cast<double>(j-i);
            g.avg_median=msum/static_cast<double>(j-i);
        }
        i=j;
    }
    return true;
}

inline const char* price_class_name(PriceClass c) {
    switch(c) {
    case PriceClass::Cheap:return "cheap";
    case PriceClass::Neutral:return "neutral";
    case PriceClass::Expensive:return "expensive";
    case PriceClass::Unknown:return "unknown";
    }
    return "unknown";
}

// Everything the JSON needs besides the timeline itself. Strings must be
// JSON-safe (ASCII identifiers); numbers < 0 mean "not set" where noted.
struct TimelineMeta {
    const char* currency="EUR";
    const char* area="";
    const char* profile="balanced";
    double target_c=-1;          // < 0: no target (mode without one)
    double comfort_min_c=0;      // 0: not set
    double comfort_max_c=0;
    bool heating_permitted=true; // mode and physical prerequisites for proposed heating
    bool indoor_fresh=true;       // false prevents price advice independent of humidity
    bool low_temperature_cycle=false;
    double preheat_c=0;
    double coast_c=0;
    size_t window_slots=8;
    double advantage_fraction=0.10;
    const char* decision_action="UNAVAILABLE";
    const char* decision_source="NONE"; // MPC, PRICE_CONTROL, THERMOSTAT, NONE
    double decision_setpoint_c=-1;
    const char* decision_reason="UNKNOWN";
    bool decision_price_valid=false;
};

// Derives the classification parameters and the headroom shown on the chart
// from the SAME profile call the controller uses (apply_profile). With no
// target (a mode that has none) there is no headroom to describe, only the
// price classes.
inline bool timeline_setup(OptimizationProfile profile,double target_c,double comfort_min_c,
                           double comfort_max_c,double base_advantage_fraction,
                           TimelineParams& params,TimelineMeta& meta) {
    BootstrapInput in{};
    size_t window=8;
    const bool has_target=std::isfinite(target_c)&&target_c>=5&&target_c<=26;
    in.desired_room_c=has_target?target_c:16.0;
    if(!apply_profile(in,profile,has_target?comfort_min_c:0,has_target?comfort_max_c:0,
                      base_advantage_fraction,window)) return false;
    params.window_slots=window;
    params.advantage_fraction=in.min_price_advantage_fraction;
    params.advantage_eur_floor=in.min_price_advantage_eur_per_kwh;
    meta.profile=profile_name(profile);
    meta.target_c=has_target?target_c:-1;
    meta.comfort_min_c=has_target?comfort_min_c:0;
    meta.comfort_max_c=has_target?comfort_max_c:0;
    meta.preheat_c=has_target?in.preheat_c:0;
    meta.coast_c=has_target?in.coast_c:0;
    meta.window_slots=window;
    meta.advantage_fraction=in.min_price_advantage_fraction;
    return true;
}

namespace timeline_detail {
template<class Emit>
inline void num(Emit& emit,double v,const char* fmt) {
    char b[48];
    if(!std::isfinite(v)) {emit("null");return;}
    std::snprintf(b,sizeof(b),fmt,v);
    emit(b);
}
template<class Emit>
inline void opt(Emit& emit,double v,bool set,const char* fmt) {
    if(set) num(emit,v,fmt); else emit("null");
}
} // namespace timeline_detail

// Streams the /api/timeline document through emit(const char*). One writer for
// the firmware and the host tools, so tests see exactly what the device sends.
template<class Emit>
inline void timeline_json(const TimelineMeta& m,const Timeline& t,Emit emit) {
    using namespace timeline_detail;
    char b[160];
    std::snprintf(b,sizeof(b),"{\"valid\":%s,\"currency\":\"%s\",\"area\":\"%s\",\"profile\":\"%s\",",
        t.slot_count?"true":"false",m.currency,m.area,m.profile);
    emit(b);
    emit("\"target_c\":");opt(emit,m.target_c,m.target_c>=0,"%.1f");
    emit(m.heating_permitted?",\"heating_permitted\":true":",\"heating_permitted\":false");
    emit(m.indoor_fresh?",\"indoor_fresh\":true":",\"indoor_fresh\":false");
    emit(m.low_temperature_cycle?",\"low_temperature_cycle\":true":",\"low_temperature_cycle\":false");
    emit(",\"comfort_min_c\":");opt(emit,m.comfort_min_c,m.comfort_min_c>0,"%.1f");
    emit(",\"comfort_max_c\":");opt(emit,m.comfort_max_c,m.comfort_max_c>0,"%.1f");
    emit(",\"preheat_c\":");num(emit,m.preheat_c,"%.2f");
    emit(",\"coast_c\":");num(emit,m.coast_c,"%.2f");
    std::snprintf(b,sizeof(b),",\"window_slots\":%zu,\"advantage_fraction\":%.4f,",
        m.window_slots,m.advantage_fraction);
    emit(b);
    std::snprintf(b,sizeof(b),"\"truncated_at_gap\":%s,\"now\":{\"action\":\"%s\",\"reason\":\"%s\",\"price_valid\":%s,\"setpoint_c\":",
        t.truncated_at_gap?"true":"false",m.decision_action,m.decision_reason,
        m.decision_price_valid?"true":"false");
    emit(b);
    opt(emit,m.decision_setpoint_c,m.decision_setpoint_c>=0,"%.1f");
    std::snprintf(b,sizeof(b),",\"source\":\"%s\"},\"segments\":[",m.decision_source);
    emit(b);
    for(size_t i=0;i<t.segment_count;++i) {
        const auto& g=t.segments[i];
        std::snprintf(b,sizeof(b),"%s{\"start\":%lld,\"end\":%lld,\"class\":\"%s\",\"slots\":%zu,",
            i?",":"",static_cast<long long>(g.start),static_cast<long long>(g.end),
            price_class_name(g.cls),g.slots);
        emit(b);
        emit("\"avg_total\":");num(emit,g.avg_total,"%.6f");
        emit(",\"avg_median\":");num(emit,g.avg_median,"%.6f");
        emit(",\"min_total\":");num(emit,g.min_total,"%.6f");
        emit(",\"max_total\":");num(emit,g.max_total,"%.6f");
        emit("}");
    }
    emit("],\"slots\":[");
    for(size_t i=0;i<t.slot_count;++i) {
        const auto& s=t.slots[i];
        std::snprintf(b,sizeof(b),"%s{\"utc\":%lld,\"total\":%.6f,\"class\":\"%s\",\"median\":%.6f}",
            i?",":"",static_cast<long long>(s.utc),s.total,price_class_name(s.cls),s.median);
        emit(b);
    }
    emit("]}");
}
} // namespace enevina::cottage
