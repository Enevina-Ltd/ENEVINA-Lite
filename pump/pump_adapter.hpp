#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
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
// Control limits must be observed or known for this exact adapter. Zero
// limits represent an unavailable capability. A setpoint is on a grid
// starting at zero (both Mitsubishi half-degrees and Midea whole/half degrees).
struct PumpHeatingLimits {
    double minimum_c=0,maximum_c=0,step_c=0;
    bool heating=false,off=false;

    bool accepts(double c) const {
        return heating && std::isfinite(c) && std::isfinite(step_c) && step_c>0 &&
            c>=minimum_c && c<=maximum_c &&
            std::fabs(c/step_c-std::round(c/step_c))<1e-7;
    }
};

enum class PumpRoomMode : uint8_t { Unknown, Off, Heat, Other };
enum class PumpStateSource : uint8_t { None, Gateway, VerifiedBus, DirectLan };
enum class PumpRoomProtocol : uint8_t { Unknown, Cn105EspHome, MideaV2Ac, MideaV3Ac, GreeEcb, GreeGcm };

struct PumpRoomState {
    PumpRoomProtocol protocol=PumpRoomProtocol::Unknown;
    HeatPumpType type=HeatPumpType::Unknown;
    uint32_t endpoint_generation=0,configuration_generation=0,link_generation=0;
    uint32_t request_generation=0;
    unsigned mode_minutes=0,request_minutes=0;
    int64_t automatic_confirmed_utc=0,automatic_queued_utc=0;
    PumpRoomMode mode=PumpRoomMode::Unknown;
    double setpoint_c=std::numeric_limits<double>::quiet_NaN();
    double indoor_c=std::numeric_limits<double>::quiet_NaN();
    double outdoor_c=std::numeric_limits<double>::quiet_NaN();
    PumpHeatingLimits heating{};
    PumpStateSource source=PumpStateSource::None;
    MeasurementEvidence evidence{};
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
