#pragma once
// Control authority is separated from price-optimisation advice.
// This pure policy does not know about HTTP, NVS, Wi-Fi, or hardware.
#include "cottage/price_bootstrap.hpp"
#include "cottage/mitsubishi_climate_command.hpp"
#include "cottage/telemetry.hpp"
#include "pump/pump_adapter.hpp"
#include <cmath>
#include <cstdint>

namespace enevina::cottage {
enum class ControlStep : uint8_t { None, Setpoint, SetHeat, SetOff };
enum class ControlBlock : uint8_t {
    Disabled, NotInHeatingMode, InputsUnverified, NoProposal,
    UnsupportedCommand, AlreadyApplied, Ready, ModeDwell, DefrostHold, AwaitingReadback
};
// Owner decision 18.11.2026: minimum 30 min between HEAT<->OFF mode changes
// (compressor run/off time; AGENTS.md section 11). Setpoint changes are not
// mode changes and are never delayed.
constexpr uint32_t mitsubishi_min_mode_dwell_ms=30u*60u*1000u;
// HEAT<->OFF history feeding ControlSnapshot::ms_since_mode_change.
// Pure and host-tested; the firmware serializes access. millis()-style
// uint32 timestamps, wrap-safe by unsigned subtraction.
//  * construction / reset(now): unknown history -> the dwell starts now
//  * command_dispatched(now): called right before the actual mode POST
//  * observe(heat,now): a fresh bridge GET. A mode change, or an observation
//    gap longer than observation_gap_ms (the pump may have toggled unseen,
//    Codex P1 post-merge #28), restarts the dwell.
class MitsubishiModeHistory {
public:
    static constexpr uint32_t observation_gap_ms=5u*60u*1000u;
    void reset(uint32_t now) { command_dispatched(now); seen_=-1; }
    void command_dispatched(uint32_t now) { last_change_=now; initial_observation_allowed_=false; }
    void observe(bool heating,uint32_t now,bool first_starts_dwell=false) {
        const int seen=heating?1:0;
        if(seen_<0?first_starts_dwell:
           (seen!=seen_ || static_cast<uint32_t>(now-last_observation_)>observation_gap_ms))
            last_change_=now;
        seen_=seen;
        last_observation_=now;
    }
    // The first observation on a new, previously disarmed controller
    // establishes state. Only a known transition or a conservative reset
    // (e.g. restored authorization or changed endpoint) starts dwell.
    void observe_bridge(bool heating,uint32_t now) {
        if(seen_<0 && initial_observation_allowed_)
            last_change_=now-mitsubishi_min_mode_dwell_ms;
        observe(heating,now);
    }
    uint32_t since_change(uint32_t now) const {
        return static_cast<uint32_t>(now-last_change_);
    }
private:
    uint32_t last_change_=0,last_observation_=0;
    int seen_=-1;
    bool initial_observation_allowed_=true;
};
struct ControlSnapshot {
    bool armed=false;
    bool heating_mode=false;
    bool temperature_fresh=false;
    bool market_fresh=false;
    bool cn105_link_fresh=false;
    bool bridge_get_ok=false; // fresh GET from device, not pump-UART proof
    bool current_heat=false;
    double current_setpoint_c=0;
    BootstrapDecision proposal{};
    // Time since the last HEAT<->OFF change that Lite sent OR observed via the
    // bridge GET. The caller also accounts for gaps and unknown history.
    uint32_t ms_since_mode_change=0;
    uint32_t min_mode_dwell_ms=mitsubishi_min_mode_dwell_ms;
};
struct ControlProposal {
    ControlBlock gate=ControlBlock::Disabled;
    ControlStep step=ControlStep::None;
    double target_c=0;
};
struct MitsubishiCommandPhase {
    ControlStep step=ControlStep::None;
    double target_c=0;
    int64_t utc=0;
};
// Only an accepted UART snapshot reaches observe(). The oldest source
// timestamp must follow the HTTP dispatch, so a buffered or same-second
// poll cannot confirm the command. Gateway REST acknowledgement is separate.
struct MitsubishiCommandReadback {
    MitsubishiCommandPhase requested{},sent{},confirmed{};
    void observe(const CN105Telemetry& reading,int64_t now) {
        constexpr int64_t window_s=90;
        if(!reading.received || sent.utc<=0 || confirmed.utc>sent.utc ||
           reading.evidence.source_utc<=sent.utc || now-sent.utc>window_s ||
           !verified_fresh(reading.evidence,now,window_s))return;
        const bool target=std::fabs(reading.setpoint_c-sent.target_c)<=0.001;
        const bool matches=(sent.step==ControlStep::Setpoint && target) ||
            (sent.step==ControlStep::SetHeat && reading.mode==ObservedPumpMode::Heat && target) ||
            (sent.step==ControlStep::SetOff && reading.mode==ObservedPumpMode::Off);
        if(matches)confirmed={sent.step,sent.target_c,reading.evidence.source_utc};
    }
};
enum class HeatTransition : uint8_t { SeparateSetpoint, Combined };
inline ControlProposal choose_pump_control(const ControlSnapshot& s,
                                          const PumpHeatingLimits& limits,
                                          HeatTransition start) {
    ControlProposal out{};
    if(!s.armed)return out;
    if(!s.heating_mode) {out.gate=ControlBlock::NotInHeatingMode;return out;}
    if(!s.temperature_fresh||!s.market_fresh||!s.cn105_link_fresh||
       !s.bridge_get_ok) {
        out.gate=ControlBlock::InputsUnverified;return out;
    }
    if(s.proposal.action==BootstrapAction::Unavailable ||
       s.proposal.action==BootstrapAction::Hold) {
        out.gate=ControlBlock::NoProposal;return out;
    }
    if(s.proposal.action==BootstrapAction::HeatSetpoint) {
        if(!limits.accepts(s.proposal.heat_setpoint_c)) {
            out.gate=ControlBlock::UnsupportedCommand;return out;
        }
        out.target_c=s.proposal.heat_setpoint_c;
        if(!s.current_heat && start==HeatTransition::Combined) {
            out.step=ControlStep::SetHeat;
        } else if(!limits.accepts(s.current_setpoint_c) ||
           std::fabs(s.current_setpoint_c-out.target_c)>0.01) {
            out.step=ControlStep::Setpoint;
        } else if(!s.current_heat) {
            out.step=ControlStep::SetHeat;
        } else {
            out.gate=ControlBlock::AlreadyApplied;return out;
        }
    } else if(s.proposal.action==BootstrapAction::Off) {
        if(!s.current_heat) {
            out.gate=ControlBlock::AlreadyApplied;return out;
        }
        if(!limits.off) {out.gate=ControlBlock::UnsupportedCommand;return out;}
        out.step=ControlStep::SetOff;
    }
    if((out.step==ControlStep::SetHeat || out.step==ControlStep::SetOff) &&
       s.ms_since_mode_change<s.min_mode_dwell_ms) {
        out.step=ControlStep::None;
        out.gate=ControlBlock::ModeDwell;
        return out;
    }
    if(out.step!=ControlStep::None)out.gate=ControlBlock::Ready;
    return out;
}
inline constexpr PumpHeatingLimits mitsubishi_heating_limits{16.0,26.0,0.5,true,true};

inline ControlProposal choose_mitsubishi_control(const ControlSnapshot& s) {
    return choose_pump_control(s,mitsubishi_heating_limits,HeatTransition::SeparateSetpoint);
}

inline const char* control_gate_name(ControlBlock x) {
    switch(x) {
    case ControlBlock::Disabled:return "DISABLED";
    case ControlBlock::NotInHeatingMode:return "NOT_HEATING_MODE";
    case ControlBlock::InputsUnverified:return "INPUTS_UNVERIFIED";
    case ControlBlock::NoProposal:return "NO_PROPOSAL";
    case ControlBlock::UnsupportedCommand:return "INVALID_SETPOINT";
    case ControlBlock::AlreadyApplied:return "ALREADY_APPLIED";
    case ControlBlock::Ready:return "READY";
    case ControlBlock::ModeDwell:return "MODE_DWELL";
    case ControlBlock::DefrostHold:return "DEFROST_HOLD";
    case ControlBlock::AwaitingReadback:return "AWAITING_READBACK";
    }
    return "UNKNOWN";
}
inline const char* control_step_name(ControlStep x) {
    switch(x) {
    case ControlStep::None:return "NONE";
    case ControlStep::Setpoint:return "SETPOINT";
    case ControlStep::SetHeat:return "HEAT";
    case ControlStep::SetOff:return "OFF";
    }
    return "NONE";
}
}
