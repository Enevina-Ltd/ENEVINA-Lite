#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "cottage/price_bootstrap.hpp"

namespace enevina::cottage {
enum class CycleStrategy : uint8_t { Existing, Short, Thermostat, Modulation, Forecast };
enum class HeatPhase : uint8_t {
    Unknown, Off, Starting, Heating, Modulating, ThermostatStop, Defrost, Recovery, Fault
};

inline const char* cycle_strategy_name(CycleStrategy strategy) {
    switch(strategy) {
    case CycleStrategy::Existing:return "EXISTING";
    case CycleStrategy::Short:return "SHORT";
    case CycleStrategy::Thermostat:return "THERMOSTAT";
    case CycleStrategy::Modulation:return "MODULATION";
    case CycleStrategy::Forecast:return "FORECAST";
    }
    return "EXISTING";
}

inline const char* heat_phase_name(HeatPhase phase) {
    switch(phase) {
    case HeatPhase::Unknown:return "UNKNOWN";
    case HeatPhase::Off:return "OFF";
    case HeatPhase::Starting:return "STARTING";
    case HeatPhase::Heating:return "HEATING";
    case HeatPhase::Modulating:return "MODULATING";
    case HeatPhase::ThermostatStop:return "THERMOSTAT_STOP";
    case HeatPhase::Defrost:return "DEFROST";
    case HeatPhase::Recovery:return "RECOVERY";
    case HeatPhase::Fault:return "FAULT";
    }
    return "UNKNOWN";
}

struct HeatPhaseSample {
    int64_t utc=0; // time of original pump reading, never an HTTP receive time
    uint32_t generation=0,endpoint=0,link=0;
    bool heat=false,off=false,fault=false,defrost=false,recovery=false,clear_of_defrost=false;
    double hz=NAN,pump_room=NAN,setpoint=NAN,room=NAN,outdoor=NAN,watts=NAN;
};

class HeatPhaseObserver {
public:
    HeatPhase observe(const HeatPhaseSample& s) {
        if(s.generation!=generation_ || s.endpoint!=endpoint_ || s.link!=link_) {
            *this={};
            generation_=s.generation;endpoint_=s.endpoint;link_=s.link;
        }
        if(s.fault || s.defrost || s.recovery || s.off || !s.heat || s.utc<=0) {
            clear_run();
            if(s.off || s.fault)recovery_pending_=false;
            if(s.defrost || s.recovery)recovery_pending_=true;
            recovery_clear_utc_=0;
            if(s.fault)return HeatPhase::Fault;
            if(s.defrost)return HeatPhase::Defrost;
            if(s.recovery)return HeatPhase::Recovery;
            return s.off?HeatPhase::Off:HeatPhase::Unknown;
        }
        if(s.utc<utc_) {
            clear_run();
            return HeatPhase::Unknown;
        }
        if(s.utc-utc_>180)clear_run();
        if(s.utc==utc_)return phase_;
        if(!std::isfinite(s.hz) || s.hz<0) {
            clear_run();
            return HeatPhase::Unknown;
        }
        const double slope=utc_ && std::isfinite(s.room) && std::isfinite(room_)?
            (s.room-room_)*60.0/(s.utc-utc_):NAN;
        utc_=s.utc;
        room_=s.room;
        // Relative to the current uninterrupted run, so a cold-weather
        // observation is never tested against the peak of a mild-weather run.
        if(std::isfinite(s.outdoor) && std::isfinite(outdoor_) &&
           std::abs(s.outdoor-outdoor_)>1.5) {
            peak_w_=peak_hz_=0;
            peak_slope_=0;
            qualified_=0;
        }
        outdoor_=s.outdoor;
        const bool running=s.hz>0;
        if(running) {
            ++running_;
            peak_hz_=std::max(peak_hz_,s.hz);
            if(std::isfinite(s.watts))peak_w_=std::max(peak_w_,s.watts);
            if(std::isfinite(slope))peak_slope_=std::max(peak_slope_,slope);
        }
        if(recovery_pending_) {
            if(!s.clear_of_defrost)recovery_clear_utc_=0;
            else if(!recovery_clear_utc_)recovery_clear_utc_=s.utc;
            else if(s.utc-recovery_clear_utc_>=60)recovery_pending_=false;
            return phase_=HeatPhase::Recovery;
        }
        const bool near_target=std::isfinite(s.pump_room) && std::isfinite(s.setpoint) &&
            s.pump_room>=s.setpoint-0.5;
        const bool slowing=running && std::isfinite(s.watts) && std::isfinite(s.outdoor) &&
            peak_w_>0 && s.watts>=0 && s.watts<0.75*peak_w_ && s.hz<0.75*peak_hz_ &&
            peak_slope_>0 && std::isfinite(slope) && slope<=0.5*peak_slope_;
        const auto evidence=s.clear_of_defrost && running_>=2?
            !running && near_target?HeatPhase::ThermostatStop:
            slowing?HeatPhase::Modulating:HeatPhase::Unknown:HeatPhase::Unknown;
        if(evidence!=candidate_) {
            candidate_=evidence;
            qualified_=1;
            candidate_utc_=s.utc;
        } else if(evidence!=HeatPhase::Unknown) {
            ++qualified_;
        }
        if(evidence!=HeatPhase::Unknown && qualified_>=2 && s.utc-candidate_utc_>=30)
            return phase_=evidence;
        return phase_=running_>=2 && running?HeatPhase::Heating:
            running_<=1?HeatPhase::Starting:HeatPhase::Unknown;
    }
private:
    void clear_run() {
        utc_=candidate_utc_=recovery_clear_utc_=0;
        room_=outdoor_=NAN;
        peak_hz_=peak_w_=peak_slope_=0;
        qualified_=running_=0;
        phase_=candidate_=HeatPhase::Unknown;
    }
    bool recovery_pending_=false;
    uint32_t generation_=0,endpoint_=0,link_=0,qualified_=0,running_=0;
    int64_t utc_=0,candidate_utc_=0,recovery_clear_utc_=0;
    double room_=NAN,outdoor_=NAN,peak_hz_=0,peak_w_=0,peak_slope_=0;
    HeatPhase candidate_=HeatPhase::Unknown,phase_=HeatPhase::Unknown;
};

struct CycleTrial {
    CycleStrategy strategy=CycleStrategy::Existing;
    double lower=0,upper=0,approved_lower=0,approved_upper=0;
    int64_t started_utc=0,expires_utc=0;
    uint64_t expires_ms=0;
    uint32_t site=0,config=0,authorization=0,endpoint=0,link=0,revision=0;
    bool current(int64_t utc,uint64_t ms,uint32_t sg,uint32_t cg,uint32_t auth) const {
        return strategy!=CycleStrategy::Existing && utc>=started_utc && utc<expires_utc &&
            ms<expires_ms && site==sg && config==cg && authorization==auth;
    }
};

enum class TrialStartTiming : uint8_t { Normal, Preheat, Coast };
inline bool natural_trial_strategy(CycleStrategy s) {
    return s==CycleStrategy::Thermostat || s==CycleStrategy::Modulation;
}
inline BootstrapDecision cycle_trial_proposal(BootstrapDecision fallback,const CycleTrial& trial,
        double room,bool heat,double native,HeatPhase phase,
        TrialStartTiming timing=TrialStartTiming::Normal) {
    if(fallback.action==BootstrapAction::Unavailable || !std::isfinite(room) ||
       !std::isfinite(native) || trial.strategy==CycleStrategy::Existing)return fallback;
    fallback.heat_setpoint_c=0;
    if(phase==HeatPhase::Defrost || phase==HeatPhase::Recovery || phase==HeatPhase::Fault) {
        fallback.action=BootstrapAction::Hold;
        fallback.reason=BootstrapReason::DeadbandHold;
    } else if(room>=trial.upper ||
              (trial.strategy==CycleStrategy::Short && room>=fallback.requested_room_c)) {
        fallback.action=BootstrapAction::Off;
        fallback.reason=trial.strategy==CycleStrategy::Short?
            BootstrapReason::LowTargetReached:BootstrapReason::TrialRoomLimit;
    } else if(!heat && natural_trial_strategy(trial.strategy) && fallback.price_data_valid &&
              timing==TrialStartTiming::Coast && room>trial.lower+0.2) {
        fallback.action=BootstrapAction::Hold;
        fallback.reason=BootstrapReason::ExpensiveWindow;
    } else if(!heat && natural_trial_strategy(trial.strategy) && fallback.price_data_valid &&
              timing==TrialStartTiming::Preheat && room<trial.upper-1.0) {
        fallback.action=BootstrapAction::HeatSetpoint;
        fallback.heat_setpoint_c=native;
        fallback.reason=BootstrapReason::CheapWindow;
    } else if(room<=std::max(trial.lower,fallback.requested_room_c-0.5)) {
        fallback.action=BootstrapAction::HeatSetpoint;
        fallback.heat_setpoint_c=native;
        fallback.reason=BootstrapReason::TemperatureLow;
    } else if(heat && room>=fallback.requested_room_c &&
              (phase==HeatPhase::ThermostatStop ||
               (trial.strategy==CycleStrategy::Modulation && phase==HeatPhase::Modulating))) {
        fallback.action=BootstrapAction::Off;
        fallback.reason=phase==HeatPhase::ThermostatStop?BootstrapReason::ThermostatCycleEnd:
            BootstrapReason::ModulationCycleEnd;
    } else {
        fallback.action=BootstrapAction::Hold;
        fallback.reason=heat && trial.strategy!=CycleStrategy::Short?
            BootstrapReason::NaturalCycleHold:BootstrapReason::DeadbandHold;
    }
    return fallback;
}
} // namespace enevina::cottage
