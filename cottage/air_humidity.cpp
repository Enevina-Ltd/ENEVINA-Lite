#include "cottage/air_humidity.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace enevina::cottage {
AirMoisture calculate_air_moisture(double t,double rh) {
    AirMoisture out{};
    if(!std::isfinite(t)||!std::isfinite(rh)||t<-45||t>75||
       rh<=0||rh>100)return out;
    const double es_hpa=6.112*std::exp(17.62*t/(243.12+t));
    const double vapour_hpa=es_hpa*rh/100.0;
    const double log=std::log(vapour_hpa/6.112);
    const double dew=243.12*log/(17.62-log);
    const double abs_gm3=216.7*vapour_hpa/(t+273.15);
    if(!std::isfinite(dew)||!std::isfinite(abs_gm3))return out;
    out.valid=true;out.temperature_c=t;out.rh_pct=rh;
    out.vapour_pressure_pa=vapour_hpa*100;
    out.dew_point_c=dew;out.absolute_humidity_gm3=abs_gm3;
    return out;
}
VentilationEstimate estimate_ventilation(const AirMoisture& in,
                                         const AirMoisture& out) {
    VentilationEstimate v{};
    if(!in.valid||!out.valid)return v;
    v.available=true;v.inside_outside_c=in.temperature_c-out.temperature_c;
    v.inside_minus_outside_gm3=
        in.absolute_humidity_gm3-out.absolute_humidity_gm3;
    v.possible_drying=v.inside_minus_outside_gm3>1.0;
    return v;
}
namespace {
int16_t clamp_i16(double v) {
    return static_cast<int16_t>(std::clamp(std::lround(v),-32768L,32767L));
}
uint16_t clamp_u16(double v) {
    return static_cast<uint16_t>(std::clamp(std::lround(v),0L,65535L));
}
uint32_t crc32(const uint8_t* bytes,size_t count) {
    uint32_t crc=~uint32_t{0};
    for(size_t i=0;i<count;i++) {
        crc^=bytes[i];
        for(int b=0;b<8;b++)crc=(crc>>1)^
            (0xEDB88320u & static_cast<uint32_t>(-(static_cast<int>(crc&1u))));
    }
    return ~crc;
}
uint32_t checksum(const AirHistoryDump& dump) {
    return crc32(reinterpret_cast<const uint8_t*>(&dump),
                 offsetof(AirHistoryDump,checksum));
}
}
bool AirHistory::add(const AirMoisture& air,int64_t utc) {
    if(!air.valid||utc<1700000000||utc>4102444800LL)return false;
    return add_hour(air,static_cast<uint32_t>(utc/3600));
}
bool AirHistory::add_offline(const AirMoisture& air,uint64_t ms) {
    if(!air.valid)return false;
    return add_hour(air,0x80000000u |
                  static_cast<uint32_t>((ms/3600000)%0x7fffffffu));
}
bool AirHistory::add_hour(const AirMoisture& air,uint32_t hour) {
    if(!air.valid)return false;
    AirHistoryPoint point{};
    point.hour_utc=hour;
    point.temperature_centi=clamp_i16(air.temperature_c*100);
    point.rh_centi=clamp_u16(air.rh_pct*100);
    point.dewpoint_centi=clamp_i16(air.dew_point_c*100);
    point.absolute_humidity_centi=clamp_u16(air.absolute_humidity_gm3*100);
    if(count_) {
        const size_t previous=(next_+cottage_history_hours-1)%
            cottage_history_hours;
        if(points_[previous].hour_utc==hour) {
            points_[previous]=point;return false; // latest per hour
        }
        if(hour<=points_[previous].hour_utc) {
            // When UTC becomes valid after offline boot, restart the civil
            // UTC series; the old uptime relative series was not civil time.
            if((points_[previous].hour_utc&0x80000000u) &&
               !(hour&0x80000000u)) {
                points_={};count_=0;next_=0;
            } else return false;
        }
    }
    points_[next_]=point;
    next_=(next_+1)%cottage_history_hours;
    if(count_<cottage_history_hours)count_++;
    return true;
}
const AirHistoryPoint& AirHistory::newest(size_t offset)const {
    static const AirHistoryPoint missing{};
    if(offset>=count_)return missing;
    return points_[(next_+cottage_history_hours-1-offset)%
                   cottage_history_hours];
}
AirHistoryDump AirHistory::dump()const {
    AirHistoryDump d{};
    d.count=count_;
    for(size_t i=0;i<count_;i++) {
        d.values[i]=newest(count_-1-i); // chronological
    }
    d.checksum=checksum(d);
    return d;
}
bool AirHistory::restore(const AirHistoryDump& saved) {
    if(saved.magic!=0x434F5448||saved.version!=1||
       saved.count>cottage_history_hours ||
       saved.checksum!=checksum(saved))return false;
    uint32_t previous=0;
    for(size_t i=0;i<saved.count;i++) {
        const auto& p=saved.values[i];
        if((p.hour_utc&0x80000000u) ||
           p.hour_utc<1700000000u/3600||
           (i && p.hour_utc<=previous)||
           p.rh_centi>10000)return false;
        previous=p.hour_utc;
    }
    points_={};count_=saved.count;
    for(size_t i=0;i<saved.count;i++)points_[i]=saved.values[i];
    next_=count_%cottage_history_hours;
    return true;
}
void MoistureMonitor::assign(MoistureAlertKind kind,bool active,uint64_t now) {
    auto& a=alerts_[static_cast<size_t>(kind)];
    if(active && !a.active){a.active=true;a.acknowledged=false;a.since_ms=now;}
    else if(!active && a.active){a={};}
}
uint64_t MoistureMonitor::high70_duration_ms(uint64_t now)const {
    return above70_ && now>=above70_since_? now-above70_since_:0;
}
uint64_t MoistureMonitor::high80_duration_ms(uint64_t now)const {
    return above80_ && now>=above80_since_? now-above80_since_:0;
}
void MoistureMonitor::observe(const RuuviV5& r,uint64_t now) {
    // A long interruption is not evidence for continuous RH exposure.
    if(has_measurement_ && (now<last_ms_||now-last_ms_>limits_.sensor_timeout_ms)) {
        above70_=above80_=false;
    }
    has_measurement_=true;last_ms_=now;
    if(r.relative_humidity_pct>=limits_.monitor_rh) {
        if(!above70_){above70_=true;above70_since_=now;}
    } else if(r.relative_humidity_pct<
              limits_.monitor_rh-limits_.clear_hysteresis_rh)above70_=false;
    if(r.temperature_c>0&&r.relative_humidity_pct>=limits_.review_rh) {
        if(!above80_){above80_=true;above80_since_=now;}
    } else if(r.temperature_c<=0 ||
              r.relative_humidity_pct<
              limits_.review_rh-limits_.clear_hysteresis_rh)above80_=false;
    assign(MoistureAlertKind::HighRh70,
           above70_&&high70_duration_ms(now)>=limits_.monitor_after_ms,now);
    assign(MoistureAlertKind::HighRh80,
           above80_&&high80_duration_ms(now)>=limits_.review_after_ms,now);
    assign(MoistureAlertKind::LowBattery,r.battery_v<limits_.low_battery_v,now);
    assign(MoistureAlertKind::Freezing,r.temperature_c<0,now);
    assign(MoistureAlertKind::SensorMissing,false,now);
}
void MoistureMonitor::tick(uint64_t now,bool bound) {
    if(!bound) {
        bound_seen_=false;
        assign(MoistureAlertKind::SensorMissing,false,now);
        return;
    }
    if(!bound_seen_) {
        bound_seen_=true;
        bound_since_ms_=now;
    }
    const bool stale=has_measurement_?
        (now<last_ms_ || now-last_ms_>limits_.sensor_timeout_ms):
        (now<bound_since_ms_ ||
         now-bound_since_ms_>limits_.sensor_timeout_ms);
    if(stale) {
        above70_=above80_=false;
        assign(MoistureAlertKind::HighRh70,false,now);
        assign(MoistureAlertKind::HighRh80,false,now);
        assign(MoistureAlertKind::Freezing,false,now);
    }
    assign(MoistureAlertKind::SensorMissing,stale,now);
}
CottageModePlan evaluate_cottage_mode(const CottageModeConfig& cfg,
       const AirMoisture& indoor,bool fresh,
       bool outdoor_ok,double outside,uint64_t elevated_rh_ms) {
    CottageModePlan p{};
    p.mode=cfg.mode;
    if(cfg.mode==CottageMode::Cold) {
        p.advisory=ModeAdvisory::NoHeating;
        return p;
    }
    if(!indoor.valid||!fresh) {
        p.advisory=ModeAdvisory::SensorUnavailable;
        return p;
    }
    if(cfg.mode==CottageMode::Drying) {
        if(!outdoor_ok||!std::isfinite(outside)||outside<-50||outside>60) {
            p.advisory=ModeAdvisory::NoOutdoorTemperature;
            return p;
        }
        if(!std::isfinite(cfg.drying_delta_c)||
           cfg.drying_delta_c<3||cfg.drying_delta_c>5) {
            p.advisory=ModeAdvisory::NoOutdoorTemperature;return p;
        }
        p.target_c=std::clamp(outside+cfg.drying_delta_c,5.0,20.0);
        p.has_target=true;
        if(elevated_rh_ms < 6ULL*3600000 ||
           indoor.rh_pct<70) {
            p.advisory=ModeAdvisory::MonitoringHumidity;
            return p; // never heat on a single high RH sample.
        }
        p.advisory=ModeAdvisory::DryingSuggested;
    } else if(cfg.mode==CottageMode::Hold) {
        if(!std::isfinite(cfg.hold_target_c)||
           cfg.hold_target_c<5||cfg.hold_target_c>24) {
            p.advisory=ModeAdvisory::SensorUnavailable;return p;
        }
        p.target_c=cfg.hold_target_c;p.has_target=true;
        p.advisory=ModeAdvisory::BelowTemperatureTarget;
    } else {
        p.advisory=ModeAdvisory::NoHeating;
        return p;
    }
    p.direct_setpoint_below_mitsubishi_min=p.target_c<16;
    p.advisory_heat_needed=indoor.temperature_c<p.target_c-0.5;
    if(p.advisory_heat_needed&&p.direct_setpoint_below_mitsubishi_min)
        p.advisory=ModeAdvisory::UnsupportedBelow16;
    // This module never schedules actual ON/OFF pulses to emulate
    // temperatures below the manufacturer's normal +16 C setpoint.
    return p;
}
} // namespace enevina::cottage
