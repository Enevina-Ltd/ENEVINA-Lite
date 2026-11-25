#pragma once
// One receding-horizon planning step, shared by the firmware optimizer task
// and the host closed-loop comparison so both run identical code:
//   identified model + comfort band + verified forecast horizon + current
//   measured state  ->  cottage::optimize()  ->  MpcPlanRecord (+ rows).
// No I/O, no device commands. Only the first decision of the plan is ever
// used, and only inside its own slot (see mpc_arbiter.hpp).
#include "cottage/building_identification.hpp"
#include "cottage/forecast_bridge.hpp"
#include "cottage/mpc.hpp"
#include "cottage/mpc_arbiter.hpp"
#include "pump/pump_adapter.hpp"
#include <cstddef>
#include <cstdint>

namespace enevina::cottage {

constexpr size_t mpc_min_horizon_slots=32;   // 8 h; shorter data -> fallback

struct PlannerInput {
    int64_t now_utc=0;
    const BuildingIdentifier* identifier=nullptr;
    bool meter_pump_only=false;      // CURRENT owner confirmation
    bool meter_incremental=false;    // CURRENT qualified shared-meter OFF baseline
    double currency_scale=1.0;
    bool heating_allowed=false;      // user mode permits heat
    bool low_temperature_cycle=false; // scoped below-minimum CN105 maintenance
    bool target_valid=false;
    double target_c=0;
    ComfortLimits limits{};          // owner's hard comfort limits (0 = unset)
    OptimizationProfile profile=OptimizationProfile::Balanced;
    bool temperature_fresh=false;
    double indoor_c=0;
    bool pump_state_known=false;
    bool pump_heat=false;
    bool pump_defrost_hold=false;
    uint32_t defrost_reports=0;
    double pump_setpoint_c=0;
    PumpHeatingLimits pump_limits{};
    unsigned minutes_in_mode=0;      // conservative (0 when unknown)
    // Mutable: the identified constant gain is written into its periods in
    // place (no 6-KiB copy on the 16-KiB optimizer task stack).
    HorizonResult* horizon=nullptr;
    size_t min_slots=mpc_min_horizon_slots;
    uint32_t mode_generation=0, config_generation=0, settings_generation=0;
    MpcRunControls controls{};
    size_t memory_budget_bytes=3*1024*1024;
};

struct PlanRow {
    int64_t utc=0;
    bool heat=false;
    double indoor_c=0;       // model prediction at the slot END
    double outdoor_c=0;      // forecast used
    double price=0;          // total price per kWh, site currency
    double energy_kwh=0;     // NaN unless a qualified direct or incremental electric model calibrated it
    double cost=0;           // NaN likewise
    double setpoint_c=0;     // chosen by the model, zero for OFF
};

struct PlannerOutput {
    MpcPlanRecord record{};
    MpcResult result{};
    Parameters params{};
    bool energy_in_kwh=false;
    bool energy_incremental=false;
    double energy_kwh=0;     // NaN unless energy_in_kwh
    double cost=0;           // NaN unless energy_in_kwh
    size_t rows=0;
};

// `rows` may be null; otherwise it must hold max_horizon_slots entries.
PlannerOutput plan_mpc(const PlannerInput& in, PlanRow* rows);
} // namespace enevina::cottage
