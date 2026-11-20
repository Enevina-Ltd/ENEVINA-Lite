#pragma once
// Chooses which proposal reaches the existing Mitsubishi control policy:
//   1. a calibrated, valid, CURRENT MPC plan (its first 15-minute decision),
//   2. otherwise the existing model-free price controller,
//   3. which itself degrades to a thermostat when prices are unavailable.
// Pure and host-tested. It never authorises a write: the caller still runs
// choose_mitsubishi_control(), the arming/identity gates, the 30-minute mode
// dwell and the readback confirmation unchanged.
#include "cottage/building_identification.hpp"
#include "cottage/mpc.hpp"
#include "cottage/price_bootstrap.hpp"
#include <cmath>
#include <cstdint>

namespace enevina::cottage {

enum class ProposalSource : uint8_t { None, Mpc, PriceControl, Thermostat };
enum class MpcRunState : uint8_t { Calibrating, Active, Fallback };
enum class MpcFallback : uint8_t {
    None, ModelLearning, ModelValidating, ModelDegraded, NoHeatingMode,
    TargetOutsideModel, OutdoorOutsideLearned, ForecastUnavailable,
    HorizonTooShort, PumpStateUnknown, TemperatureStale, SolverFailed,
    SolverTimeout, PlanStale, PlanInputsChanged, BelowComfortFloor,
    AboveComfortCeiling, ResourceLimit, SyntheticModelOnly
};

// Everything the planner fixed when it computed the plan. A plan is only
// usable inside its own first slot and with unchanged inputs.
struct MpcPlanRecord {
    bool has_plan=false;
    MpcFallback planning_fallback=MpcFallback::ModelLearning; // when !has_plan
    MpcStatus status=MpcStatus::InvalidInput;
    int64_t first_utc=0;      // slot the first decision applies to
    int64_t computed_utc=0;
    uint32_t mode_generation=0, config_generation=0, settings_generation=0;
    uint32_t model_revision=0;
    bool initial_heat=false;  // pump state the plan started from
    bool first_heat=false;    // plan decision for first_utc
    double target_c=0;
    ComfortBand band{};
    size_t count=0;
};

struct ArbiterInput {
    int64_t now_utc=0;
    bool temperature_fresh=false;
    double indoor_c=0;
    bool calibrated=false;
    CalibrationState calibration=CalibrationState::NoData;
    MpcPlanRecord plan{};
    uint32_t mode_generation=0, config_generation=0, settings_generation=0;
    // Pump HEAT/OFF from a SOURCE-VERIFIED, fresh pump poll only; a gateway
    // HTTP/SSE receipt is not pump-origin evidence (Codex P1, PR #63).
    bool pump_state_known=false;
    bool pump_heat=false;
    // The pump readback confirmed THIS plan's first decision in its own slot
    // (set by the control path only after a matching readback).
    bool own_transition_confirmed=false;
    BootstrapDecision bootstrap{};  // the existing price/thermostat decision
};

struct ArbiterOutput {
    BootstrapDecision proposal{};
    ProposalSource source=ProposalSource::None;
    MpcRunState mpc_state=MpcRunState::Calibrating;
    MpcFallback fallback=MpcFallback::ModelLearning;
};

inline ProposalSource bootstrap_source(const BootstrapDecision& d) {
    if(d.action==BootstrapAction::Unavailable) return ProposalSource::None;
    return d.price_data_valid?ProposalSource::PriceControl:ProposalSource::Thermostat;
}

inline MpcFallback fallback_for_status(MpcStatus s) {
    switch(s) {
    case MpcStatus::Ok:return MpcFallback::None;
    case MpcStatus::UnsafeStart:return MpcFallback::BelowComfortFloor;
    case MpcStatus::Timeout:
    case MpcStatus::Cancelled:return MpcFallback::SolverTimeout;
    case MpcStatus::ResourceLimit:return MpcFallback::ResourceLimit;
    case MpcStatus::InvalidInput:
    case MpcStatus::Infeasible:
    case MpcStatus::ReplayFailed:return MpcFallback::SolverFailed;
    }
    return MpcFallback::SolverFailed;
}

// Is the stored plan still valid for the CURRENT mode, inputs and pump?
// (structural validity; a room outside the band is judged separately in
// arbitrate()). The dashboard uses this too, so a plan the controller has
// invalidated is never shown as valid (Codex P2, PR #63).
inline MpcFallback plan_validity(const ArbiterInput& in) {
    // "Cold" and a drying mode that does not permit heat both yield
    // NoHeatingRequested in the existing controller.
    if(in.bootstrap.reason==BootstrapReason::NoHeatingRequested &&
       in.bootstrap.action==BootstrapAction::Off)
        return MpcFallback::NoHeatingMode;
    const MpcPlanRecord& p=in.plan;
    if(!p.has_plan) return p.planning_fallback==MpcFallback::None?
        MpcFallback::SolverFailed:p.planning_fallback;
    if(p.status!=MpcStatus::Ok || p.count==0) return fallback_for_status(p.status);
    // Never use a stale plan: only inside the slot it was computed for.
    if(in.now_utc<p.first_utc || in.now_utc>=p.first_utc+900 ||
       p.computed_utc>in.now_utc || p.computed_utc<p.first_utc)
        return MpcFallback::PlanStale;
    if(p.config_generation!=in.config_generation ||
       p.settings_generation!=in.settings_generation)
        return MpcFallback::PlanInputsChanged;
    if(!in.pump_state_known) return MpcFallback::PumpStateUnknown;
    // The pump-mode generation also advances when the plan's OWN decision
    // is executed. Accept that single expected transition only when the
    // readback confirmed it in this slot and the verified pump now shows it;
    // any other generation change (external switch, source gap) re-plans.
    const bool own_transition=p.first_heat!=p.initial_heat &&
        in.own_transition_confirmed && in.pump_heat==p.first_heat;
    if(p.mode_generation!=in.mode_generation && !own_transition)
        return MpcFallback::PlanInputsChanged;
    if(!(in.bootstrap.requested_room_c>0) ||
       std::fabs(in.bootstrap.requested_room_c-p.target_c)>0.01)
        return MpcFallback::PlanInputsChanged;
    // A pump state other than the planned start state or the plan's own
    // first decision means an external change (remote, manual): re-plan.
    if(in.pump_heat!=p.initial_heat && in.pump_heat!=p.first_heat)
        return MpcFallback::PlanInputsChanged;
    return MpcFallback::None;
}

inline ArbiterOutput arbitrate(const ArbiterInput& in) {
    ArbiterOutput out{};
    out.proposal=in.bootstrap;
    out.source=bootstrap_source(in.bootstrap);
    const auto fallback=[&](MpcFallback why) {
        out.fallback=why;
        out.mpc_state=(why==MpcFallback::ModelLearning ||
                       why==MpcFallback::ModelValidating)?
            MpcRunState::Calibrating:MpcRunState::Fallback;
        return out;
    };
    if(!in.calibrated) {
        if(in.calibration==CalibrationState::Degraded)
            return fallback(MpcFallback::ModelDegraded);
        return fallback(in.calibration==CalibrationState::Validating?
            MpcFallback::ModelValidating:MpcFallback::ModelLearning);
    }
    if(in.bootstrap.reason==BootstrapReason::NoHeatingRequested &&
       in.bootstrap.action==BootstrapAction::Off)
        return fallback(MpcFallback::NoHeatingMode);
    if(!in.temperature_fresh || !std::isfinite(in.indoor_c))
        return fallback(MpcFallback::TemperatureStale);
    const MpcFallback invalid=plan_validity(in);
    if(invalid!=MpcFallback::None) return fallback(invalid);
    const MpcPlanRecord& p=in.plan;
    // Measured room outside the band the plan protects: the plan's model is
    // wrong right now; the existing controller (which heats below its own
    // lower edge and stops above its upper edge) takes over this slot.
    if(!p.first_heat && in.indoor_c<=p.band.lower_c-0.2)
        return fallback(MpcFallback::BelowComfortFloor);
    if(p.first_heat && in.indoor_c>=p.band.upper_c+0.3)
        return fallback(MpcFallback::AboveComfortCeiling);
    BootstrapDecision d{};
    d.requested_room_c=p.target_c;
    d.price_data_valid=true;
    d.executable=false;
    if(p.first_heat) {
        d.action=BootstrapAction::HeatSetpoint;
        d.heat_setpoint_c=16.0; // the model's HEAT 16 C operating point
        d.reason=BootstrapReason::TemperatureLow; // internal; not displayed
    } else {
        d.action=BootstrapAction::Off;
        d.reason=BootstrapReason::TemperatureHigh; // internal; not displayed
    }
    out.proposal=d;
    out.source=ProposalSource::Mpc;
    out.mpc_state=MpcRunState::Active;
    out.fallback=MpcFallback::None;
    return out;
}

inline const char* proposal_source_name(ProposalSource s) {
    switch(s) {
    case ProposalSource::None:return "NONE";
    case ProposalSource::Mpc:return "MPC";
    case ProposalSource::PriceControl:return "PRICE_CONTROL";
    case ProposalSource::Thermostat:return "THERMOSTAT";
    }
    return "NONE";
}
inline const char* mpc_run_state_name(MpcRunState s) {
    switch(s) {
    case MpcRunState::Calibrating:return "CALIBRATING";
    case MpcRunState::Active:return "ACTIVE";
    case MpcRunState::Fallback:return "FALLBACK";
    }
    return "FALLBACK";
}
inline const char* mpc_fallback_name(MpcFallback f) {
    switch(f) {
    case MpcFallback::None:return "NONE";
    case MpcFallback::ModelLearning:return "MODEL_LEARNING";
    case MpcFallback::ModelValidating:return "MODEL_VALIDATING";
    case MpcFallback::ModelDegraded:return "MODEL_DEGRADED";
    case MpcFallback::NoHeatingMode:return "NO_HEATING_MODE";
    case MpcFallback::TargetOutsideModel:return "TARGET_OUTSIDE_MODEL";
    case MpcFallback::OutdoorOutsideLearned:return "OUTDOOR_OUTSIDE_LEARNED_RANGE";
    case MpcFallback::ForecastUnavailable:return "FORECAST_UNAVAILABLE";
    case MpcFallback::HorizonTooShort:return "HORIZON_TOO_SHORT";
    case MpcFallback::PumpStateUnknown:return "PUMP_STATE_UNKNOWN";
    case MpcFallback::TemperatureStale:return "TEMPERATURE_STALE";
    case MpcFallback::SolverFailed:return "SOLVER_FAILED";
    case MpcFallback::SolverTimeout:return "SOLVER_TIMEOUT";
    case MpcFallback::PlanStale:return "PLAN_STALE";
    case MpcFallback::PlanInputsChanged:return "PLAN_INPUTS_CHANGED";
    case MpcFallback::BelowComfortFloor:return "BELOW_COMFORT_FLOOR";
    case MpcFallback::AboveComfortCeiling:return "ABOVE_COMFORT_CEILING";
    case MpcFallback::ResourceLimit:return "RESOURCE_LIMIT";
    case MpcFallback::SyntheticModelOnly:return "SYNTHETIC_MODEL_ONLY";
    }
    return "UNKNOWN";
}
} // namespace enevina::cottage
