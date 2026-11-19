#pragma once
// Range queries over the history ring: downsampling, summary and the
// /api/history JSON. Averages use only PRESENT values; a bucket with none of a
// field reports it as missing (null). No interpolation, ever.
#include "cottage/history_store.hpp"
#include <cmath>
#include <cstdio>

namespace enevina::cottage {
constexpr size_t kMaxHistoryPoints=288;
constexpr double kNaN=std::numeric_limits<double>::quiet_NaN();

struct HistoryPoint {
    int64_t t=0;                 // UTC of the bucket start
    double indoor=kNaN,rh=kNaN,outdoor=kNaN,setpoint=kNaN,power=kNaN,price=kNaN;
    uint8_t action=0;            // last known proposal in the bucket
    bool outdoor_forecast=false,armed=false;
    uint32_t records=0;          // 15-minute records that contributed
};
struct Range {
    double min=kNaN,max=kNaN,sum=0;
    size_t n=0;
    void add(double v) {
        if(!std::isfinite(v)) return;
        min=n?std::min(min,v):v;max=n?std::max(max,v):v;sum+=v;++n;
    }
    double avg() const {return n?sum/static_cast<double>(n):kNaN;}
};
struct HistorySummary {
    size_t slots_total=0,slots_present=0;
    Range indoor,outdoor,price,setpoint;
    double kwh=0;                // energy of the METERED load over present power slots
    size_t kwh_slots=0;          // how many slots had a power reading
    size_t armed_slots=0;
    double coverage() const {return slots_total?static_cast<double>(slots_present)/static_cast<double>(slots_total):0;}
};

// Buckets of `step_slots` x 15 min ending at the slot of `end_utc`. Returns the
// number of buckets written (oldest first). `range_slots` must be a multiple of
// step_slots.
inline size_t build_history_series(const HistoryStore& store,int64_t end_utc,size_t range_slots,
                                   size_t step_slots,HistoryPoint* out,size_t capacity,
                                   HistorySummary& summary) {
    summary=HistorySummary{};
    if(!out||step_slots==0||range_slots==0||range_slots%step_slots!=0||
       range_slots/step_slots>capacity||range_slots>store.capacity()||end_utc<=0) return 0;
    const int64_t end_slot=end_utc/kHistoryStepS;
    if(end_slot<static_cast<int64_t>(range_slots)) return 0;
    const int64_t first=end_slot-static_cast<int64_t>(range_slots)+1;
    const size_t buckets=range_slots/step_slots;
    summary.slots_total=range_slots;
    for(size_t b=0;b<buckets;++b) {
        HistoryPoint& p=out[b];
        p=HistoryPoint{};
        const int64_t b0=first+static_cast<int64_t>(b*step_slots);
        p.t=b0*kHistoryStepS;
        Range in,rh,od,pw,pr;
        for(size_t k=0;k<step_slots;++k) {
            const HistoryRecord* r=store.get(static_cast<uint32_t>(b0+static_cast<int64_t>(k)));
            if(!r) continue;
            ++p.records;++summary.slots_present;
            if(r->indoor_c100!=kMissingI16) {in.add(r->indoor_c100/100.0);summary.indoor.add(r->indoor_c100/100.0);}
            if(r->rh_c100!=kMissingU16) rh.add(r->rh_c100/100.0);
            if(r->outdoor_c100!=kMissingI16) {
                od.add(r->outdoor_c100/100.0);summary.outdoor.add(r->outdoor_c100/100.0);
                if(r->flags&kOutdoorIsForecast) p.outdoor_forecast=true;
            }
            if(r->setpoint_c10!=kMissingI16) {p.setpoint=r->setpoint_c10/10.0;summary.setpoint.add(p.setpoint);}
            if(r->power_w!=kMissingU16) {
                pw.add(r->power_w);
                summary.kwh+=r->power_w*0.25/1000.0;++summary.kwh_slots;
            }
            if(r->price_e5!=kMissingI32) {pr.add(r->price_e5/1e5);summary.price.add(r->price_e5/1e5);}
            p.action=static_cast<uint8_t>(r->flags&kActionMask);
            if(r->flags&kControlArmed) {p.armed=true;++summary.armed_slots;}
        }
        p.indoor=in.avg();p.rh=rh.avg();p.outdoor=od.avg();p.power=pw.avg();p.price=pr.avg();
    }
    return buckets;
}

enum class HistoryRange : uint8_t { Day=0, Week=1, Month=2 };
struct RangeSpec {const char* name;size_t range_slots;size_t step_slots;};
inline bool history_range_spec(const char* name,RangeSpec& out) {
    if(!name) return false;
    if(!std::strcmp(name,"24h")) out={"24h",96,1};          // 96 x 15 min
    else if(!std::strcmp(name,"7d")) out={"7d",672,4};      // 168 x 1 h
    else if(!std::strcmp(name,"30d")) out={"30d",2880,12};  // 240 x 3 h
    else return false;
    return true;
}

namespace history_detail {
template<class Emit>
inline void val(Emit& emit,double v,const char* fmt) {
    if(!std::isfinite(v)) {emit("null");return;}
    char b[40];
    std::snprintf(b,sizeof(b),fmt,v);
    emit(b);
}
} // namespace history_detail

// Streams the /api/history document. `scope` is the owner's statement about the
// consumption meter ("heat_pump", "whole_house", "unknown"), echoed so the UI
// labels the energy correctly.
template<class Emit>
inline void history_json(const char* range_name,size_t step_slots,const HistoryPoint* pts,size_t n,
                         const HistorySummary& s,const EventLog& events,int64_t from_utc,
                         const char* currency,const char* meter_scope,bool persistent,Emit emit) {
    using namespace history_detail;
    char b[200];
    std::snprintf(b,sizeof(b),"{\"valid\":%s,\"range\":\"%s\",\"step_s\":%lld,\"currency\":\"%s\",\"meter_scope\":\"%s\",\"persistent\":%s,",
        n?"true":"false",range_name,static_cast<long long>(step_slots*kHistoryStepS),currency,meter_scope,
        persistent?"true":"false");
    emit(b);
    std::snprintf(b,sizeof(b),"\"slots_total\":%zu,\"slots_present\":%zu,\"kwh_slots\":%zu,\"armed_slots\":%zu,",
        s.slots_total,s.slots_present,s.kwh_slots,s.armed_slots);
    emit(b);
    emit("\"kwh\":");val(emit,s.kwh_slots?s.kwh:kNaN,"%.3f");
    emit(",\"indoor\":{\"min\":");val(emit,s.indoor.min,"%.2f");
    emit(",\"max\":");val(emit,s.indoor.max,"%.2f");emit(",\"avg\":");val(emit,s.indoor.avg(),"%.2f");
    emit("},\"outdoor\":{\"min\":");val(emit,s.outdoor.min,"%.2f");
    emit(",\"max\":");val(emit,s.outdoor.max,"%.2f");emit(",\"avg\":");val(emit,s.outdoor.avg(),"%.2f");
    emit("},\"price\":{\"min\":");val(emit,s.price.min,"%.5f");
    emit(",\"max\":");val(emit,s.price.max,"%.5f");emit(",\"avg\":");val(emit,s.price.avg(),"%.5f");
    emit("},\"points\":[");
    for(size_t i=0;i<n;++i) {
        const HistoryPoint& p=pts[i];
        std::snprintf(b,sizeof(b),"%s{\"t\":%lld,\"n\":%u,\"a\":%u,\"f\":%d,\"r\":%d,\"in\":",i?",":"",
            static_cast<long long>(p.t),p.records,p.action,p.outdoor_forecast?1:0,p.armed?1:0);
        emit(b);
        val(emit,p.indoor,"%.2f");emit(",\"rh\":");val(emit,p.rh,"%.1f");
        emit(",\"out\":");val(emit,p.outdoor,"%.2f");emit(",\"sp\":");val(emit,p.setpoint,"%.1f");
        emit(",\"w\":");val(emit,p.power,"%.0f");emit(",\"p\":");val(emit,p.price,"%.5f");emit("}");
    }
    emit("],\"events\":[");
    size_t shown=0;
    for(size_t i=0;i<events.count()&&shown<60;++i) {
        const HistoryEvent& e=events.newest(i);
        if(static_cast<int64_t>(e.utc)<from_utc) continue;
        std::snprintf(b,sizeof(b),"%s{\"t\":%u,\"k\":%u,\"s\":%u,\"v\":%d,\"a\":%u}",shown?",":"",e.utc,e.kind,e.source,e.value,e.arg);
        emit(b);
        ++shown;
    }
    emit("]}");
}
} // namespace enevina::cottage
