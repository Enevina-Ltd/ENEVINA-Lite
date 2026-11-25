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
#include "cottage/optimization_profile.hpp"
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
    AboveComfortCeiling, ResourceLimit, SyntheticModelOnly, ComfortLimitsInvalid, DefrostHold,
    LowTargetReached, LowTemperatureBand, MaintenanceElectricLearning
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
    uint32_t defrost_reports=0;
    bool initial_heat=false;  // pump state the plan started from
    bool first_heat=false;    // plan decision for first_utc
    bool variable_setpoints=false;
    bool low_temperature_cycle=false;
    bool maintenance_storage=false;
    OptimizationProfile profile=OptimizationProfile::Balanced;
    double initial_setpoint_c=16;
    double first_setpoint_c=16;
    double target_c=0;
    ComfortBand band{};
    ComfortLimits limits{};   // owner's hard limits the band was built from
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
    // The owner's CURRENT hard comfort limits (0 = unset). A plan built from
    // other limits is revoked; limits that cannot hold the target are refused.
    ComfortLimits limits{};
    // Direct pump observations or a separately identified gateway REQUEST
    // observed via continuous GETs. The latter is not pump-origin evidence.
    bool pump_state_known=false;
    bool pump_gateway_request=false;
    bool pump_heat=false;
    bool pump_defrost_hold=false;
    uint32_t defrost_reports=0;
    double pump_setpoint_c=0;
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

inline void apply_mpc_profile(ComfortBand& band,OptimizationProfile profile) {
    const double share=profile_tuning(profile).headroom_share;
    band.lower_c=std::max(band.lower_c,
        band.target_c-std::max(0.5,(band.target_c-band.lower_c)*share));
    band.upper_c=std::min(band.upper_c,
        band.target_c+std::max(0.5,(band.upper_c-band.target_c)*share));
}

// THE single predicate for "may the MPC act now?". arbitrate() uses it and
// so do /api/plan and /api/mpc, so a plan the controller rejects is never
// shown as valid (Codex P2, PR #63). Order: calibration, the user's mode,
// sensor freshness, the plan's own structure/age/inputs, the owner's hard
// limits and finally the measured room against the band the plan protects.
inline MpcFallback plan_validity(const ArbiterInput& in) {
    if(in.pump_defrost_hold && in.pump_heat)return MpcFallback::DefrostHold;
    if(!in.calibrated) {
        if(in.calibration==CalibrationState::Degraded) return MpcFallback::ModelDegraded;
        return in.calibration==CalibrationState::Validating?
            MpcFallback::ModelValidating:MpcFallback::ModelLearning;
    }
    // "Cold" and a drying mode that does not permit heat both yield
    // NoHeatingRequested in the existing controller.
    if(in.bootstrap.reason==BootstrapReason::NoHeatingRequested &&
       in.bootstrap.action==BootstrapAction::Off)
        return MpcFallback::NoHeatingMode;
    if(!in.temperature_fresh || !std::isfinite(in.indoor_c))
        return MpcFallback::TemperatureStale;
    const MpcPlanRecord& p=in.plan;
    if(!p.has_plan) return p.planning_fallback==MpcFallback::None?
        MpcFallback::SolverFailed:p.planning_fallback;
    if(p.status!=MpcStatus::Ok || p.count==0) return fallback_for_status(p.status);
    // Never use a stale plan: only inside the slot it was computed for.
    if(in.now_utc<p.first_utc || in.now_utc>=p.first_utc+900 ||
       p.computed_utc>in.now_utc || p.computed_utc<p.first_utc)
        return MpcFallback::PlanStale;
    if(p.config_generation!=in.config_generation ||
       p.settings_generation!=in.settings_generation ||
       p.defrost_reports!=in.defrost_reports)
        return MpcFallback::PlanInputsChanged;
    if(!in.pump_state_known) return MpcFallback::PumpStateUnknown;
    if(p.low_temperature_cycle!=
       (in.pump_gateway_request && low_temperature_cycle(in.bootstrap)) ||
       p.maintenance_storage!=(p.low_temperature_cycle && in.bootstrap.maintenance_storage))
        return MpcFallback::PlanInputsChanged;
    // The full-slot model cannot insist on a heating request beyond a
    // below-device-minimum room ceiling. The same plan_validity predicate
    // is used for dispatch and every published plan. Defrost wins above.
    if((p.first_heat || in.pump_heat) && in.pump_gateway_request && low_temperature_stop(in.bootstrap) &&
       (!p.maintenance_storage || in.indoor_c>=p.band.upper_c))
        return MpcFallback::LowTargetReached;
    // Do not start a brief, inefficient 16 C pulse merely because the
    // electricity price changed within the independent room deadband.
    if(p.low_temperature_cycle && !p.maintenance_storage && p.first_heat && !in.pump_heat &&
       in.bootstrap.action!=BootstrapAction::HeatSetpoint)
        return MpcFallback::LowTemperatureBand;
    // The pump-mode generation advances on every source gap, mode change and
    // re-verification. The ONLY change a plan survives is the single
    // transition its own first decision causes: exactly one increment, the
    // plan expected a mode change, the readback confirmed it in this slot and
    // the verified pump now shows it. Anything more (an extra switch, a
    // manual HEAT->OFF->HEAT that ends in the original state, a source gap)
    // re-plans.
    const uint32_t delta=in.mode_generation-p.mode_generation;   // wraps correctly
    const bool expects_transition=p.first_heat!=p.initial_heat ||
        (p.variable_setpoints && p.first_heat &&
         std::fabs(p.first_setpoint_c-p.initial_setpoint_c)>0.001);
    const bool at_first_setpoint=!p.variable_setpoints || !p.first_heat ||
        std::fabs(in.pump_setpoint_c-p.first_setpoint_c)<=0.001;
    const bool own_transition=delta==1 && expects_transition &&
        in.own_transition_confirmed && in.pump_heat==p.first_heat && at_first_setpoint;
    if(delta!=0 && !own_transition) return MpcFallback::PlanInputsChanged;
    if(p.variable_setpoints && !own_transition && in.pump_heat &&
       !(std::fabs(in.pump_setpoint_c-p.initial_setpoint_c)<=0.001))
        return MpcFallback::PlanInputsChanged;
    if(!(in.bootstrap.requested_room_c>0) ||
       std::fabs(in.bootstrap.requested_room_c-p.target_c)>0.01)
        return MpcFallback::PlanInputsChanged;
    // A pump state other than the planned start state or the plan's own
    // first decision means an external change (remote, manual): re-plan.
    if(in.pump_heat!=p.initial_heat && in.pump_heat!=p.first_heat)
        return MpcFallback::PlanInputsChanged;
    // The owner's hard comfort limits. Malformed or contradictory limits: no
    // MPC (the existing controller takes over); changed limits revoke the plan.
    ComfortBand band{};
    if(!mpc_comfort_band(p.target_c,band,in.limits,p.variable_setpoints,p.maintenance_storage))
        return MpcFallback::ComfortLimitsInvalid;
    if(p.variable_setpoints)apply_mpc_profile(band,p.profile);
    if(!(in.limits==p.limits) ||
       std::fabs(band.lower_c-p.band.lower_c)>1e-9 ||
       std::fabs(band.upper_c-p.band.upper_c)>1e-9)
        return MpcFallback::PlanInputsChanged;
    // The measured room must be inside the band the plan protects: outside
    // it the model is wrong right now and the existing controller (which
    // heats at/below its lower edge and stops at/above its upper edge) acts.
    // No tolerance beyond the owner's limit.
    if(!p.first_heat && in.indoor_c<=band.lower_c)
        return MpcFallback::BelowComfortFloor;
    if(p.first_heat && in.indoor_c>=band.upper_c)
        return MpcFallback::AboveComfortCeiling;
    return MpcFallback::None;
}

inline ArbiterOutput arbitrate(const ArbiterInput& in) {
    ArbiterOutput out{};
    out.proposal=in.bootstrap;
    out.source=bootstrap_source(in.bootstrap);
    const MpcFallback invalid=plan_validity(in);
    if(invalid==MpcFallback::DefrostHold) {
        out.proposal.action=BootstrapAction::Hold;
        out.proposal.heat_setpoint_c=0;
        out.proposal.executable=false;
        out.source=ProposalSource::None;
        out.mpc_state=MpcRunState::Fallback;
        out.fallback=invalid;
        return out;
    }
    if(invalid!=MpcFallback::None) {
        out.fallback=invalid;
        out.mpc_state=(invalid==MpcFallback::ModelLearning ||
                       invalid==MpcFallback::ModelValidating)?
            MpcRunState::Calibrating:MpcRunState::Fallback;
        return out;
    }
    const MpcPlanRecord& p=in.plan;
    BootstrapDecision d{};
    d.requested_room_c=p.target_c;
    d.low_temperature_cycle=p.low_temperature_cycle;
    d.maintenance_storage=p.maintenance_storage;
    d.maintenance_ceiling_c=p.maintenance_storage?p.band.upper_c:0;
    d.price_data_valid=true;
    d.executable=false;
    if(p.first_heat) {
        d.action=BootstrapAction::HeatSetpoint;
        d.heat_setpoint_c=p.first_setpoint_c;
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
    case MpcFallback::ComfortLimitsInvalid:return "COMFORT_LIMITS_INVALID";
    case MpcFallback::DefrostHold:return "DEFROST_HOLD";
    case MpcFallback::LowTargetReached:return "LOW_TARGET_REACHED";
    case MpcFallback::LowTemperatureBand:return "LOW_TEMPERATURE_BAND";
    case MpcFallback::MaintenanceElectricLearning:return "MAINTENANCE_ELECTRIC_LEARNING";
    }
    return "UNKNOWN";
}
} // namespace enevina::cottage
