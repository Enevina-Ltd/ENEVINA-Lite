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
    // Propagate verified original pump-read evidence, not network receipt time.
    MeasurementEvidence evidence{};
};

// One-pump, manufacturer-independent telemetry and capability interface.
// Command-path development remains in scope; actual writes require the
// separately tested, model-specific command adapter and Supervisor approval.
// Merely reporting a capability must never authorize physical commands.
class PumpAdapter {
public:
    virtual ~PumpAdapter() = default;
    // Introspection is read-only. Reporting a capability does not authorize
    // writes; a separately validated Supervisor gate remains mandatory.
    virtual PumpDescriptor descriptor() const noexcept { return {}; }
    virtual bool poll(PumpTelemetry& output) = 0;
};
}
