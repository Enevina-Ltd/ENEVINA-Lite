#pragma once
// Control authority is separated from price-optimisation advice.
// This pure policy does not know about HTTP, NVS, Wi-Fi, or hardware.
#include "cottage/price_bootstrap.hpp"
#include "cottage/mitsubishi_climate_command.hpp"
#include <cmath>
#include <cstdint>

namespace enevina::cottage {
enum class ControlStep : uint8_t { None, Setpoint, SetHeat, SetOff };
enum class ControlBlock : uint8_t {
    Disabled, NotInHoldMode, InputsUnverified, NoProposal,
    UnsupportedCommand, AlreadyApplied, ModeDwell, RateLimited,
    Ready
};
struct ControlSnapshot {
    bool armed=false;
    bool hold_mode=false;
    bool temperature_fresh=false;
    bool market_fresh=false;
    bool cn105_link_fresh=false;
    bool bridge_get_ok=false; // fresh GET from device, not pump-UART proof
    bool pump_origin_verified=false; // successful fresh 0x02/03/06 UART source proof
    bool current_heat=false;
    double current_setpoint_c=0;
    uint64_t now_ms=0;
    uint64_t last_command_ms=0;
    bool has_last_command=false;
    uint64_t last_mode_change_ms=0;
    bool has_confirmed_mode_change=false;
    BootstrapDecision proposal{};
};
struct ControlProposal {
    ControlBlock gate=ControlBlock::Disabled;
    ControlStep step=ControlStep::None;
    double target_c=0;
};
inline ControlProposal choose_mitsubishi_control(const ControlSnapshot& s) {
    ControlProposal out{};
    if(!s.armed)return out;
    if(!s.hold_mode) {out.gate=ControlBlock::NotInHoldMode;return out;}
    if(!s.temperature_fresh||!s.market_fresh||!s.cn105_link_fresh||
       !s.bridge_get_ok||!s.pump_origin_verified) {
        out.gate=ControlBlock::InputsUnverified;return out;
    }
    if(s.proposal.action==BootstrapAction::Unavailable ||
       s.proposal.action==BootstrapAction::Hold) {
        out.gate=ControlBlock::NoProposal;return out;
    }
    if(s.has_last_command) {
        if(s.now_ms<s.last_command_ms ||
           s.now_ms-s.last_command_ms<60000ULL) {
            out.gate=ControlBlock::RateLimited;return out;
        }
    }
    if(s.proposal.action==BootstrapAction::HeatSetpoint) {
        if(!mitsubishi_valid_setpoint(s.proposal.heat_setpoint_c)) {
            out.gate=ControlBlock::UnsupportedCommand;return out;
        }
        out.target_c=s.proposal.heat_setpoint_c;
        if(!std::isfinite(s.current_setpoint_c) ||
           !mitsubishi_valid_setpoint(s.current_setpoint_c) ||
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
        out.step=ControlStep::SetOff;
    }
    if((out.step==ControlStep::SetHeat || out.step==ControlStep::SetOff) &&
        s.has_confirmed_mode_change &&
        (s.now_ms<s.last_mode_change_ms ||
         s.now_ms-s.last_mode_change_ms<3600000ULL)) {
        out.step=ControlStep::None;
        out.gate=ControlBlock::ModeDwell;
        return out;
    }
    if(out.step!=ControlStep::None)out.gate=ControlBlock::Ready;
    return out;
}
inline const char* control_gate_name(ControlBlock x) {
    switch(x) {
    case ControlBlock::Disabled:return "DISABLED";
    case ControlBlock::NotInHoldMode:return "NOT_HOLD_MODE";
    case ControlBlock::InputsUnverified:return "INPUTS_UNVERIFIED";
    case ControlBlock::NoProposal:return "NO_PROPOSAL";
    case ControlBlock::UnsupportedCommand:return "INVALID_SETPOINT";
    case ControlBlock::AlreadyApplied:return "ALREADY_APPLIED";
    case ControlBlock::ModeDwell:return "MIN_MODE_DURATION";
    case ControlBlock::RateLimited:return "RATE_LIMIT";
    case ControlBlock::Ready:return "READY";
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
