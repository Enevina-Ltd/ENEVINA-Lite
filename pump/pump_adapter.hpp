#pragma once
#include <cstdint>
#include "pump/measurement_quality.hpp"

namespace enevina {
// Unknown is the only safe default: a manufacturer's name or capability
// must be reported from an adapter that has actually verified the model.
enum class HeatPumpType : uint8_t { Unknown, AirToAir, AirToWater, GroundSource };
struct PumpCapabilities {
    bool reports_indoor_temperature=false;
    bool reports_outdoor_temperature=false;
    bool reports_compressor_frequency=false;
    bool reports_defrost=false;
    bool reports_dhw_temperature=false;
    bool heating_setpoint_write_verified=false;
    bool dhw_setpoint_write_verified=false;
    bool off_command_verified=false;
    bool pump_side_freshness_verified=false;
};
struct PumpDescriptor {
    const char* manufacturer=nullptr;
    const char* model=nullptr;
    HeatPumpType type=HeatPumpType::Unknown;
    PumpCapabilities capabilities{};
};
struct PumpTelemetry {
    double inlet_c=0;
    double outlet_c=0;
    double outdoor_c=0;
    double dhw_c=0;
    double compressor_hz=0;
    bool defrosting=false;
    int operating_mode=-1;
    // Generic provenance; adapters report Unknown until source-side proof exists.
    MeasurementEvidence evidence{};
};

// V1 is deliberately read-only. A future command interface must be gated by
// Supervisor and verified for each exact pump/HeishaMon combination.
class PumpAdapter {
public:
    virtual ~PumpAdapter() = default;
    // Introspection is read-only. Reporting a capability does not authorize
    // writes; a separately validated Supervisor gate remains mandatory.
    virtual PumpDescriptor descriptor() const noexcept { return {}; }
    virtual bool poll(PumpTelemetry& output) = 0;
};
}
