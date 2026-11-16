#pragma once

namespace enevina {
struct PumpTelemetry {
    double inlet_c=0;
    double outlet_c=0;
    double outdoor_c=0;
    double dhw_c=0;
    double compressor_hz=0;
    bool defrosting=false;
    int operating_mode=-1;
};

// V1 is deliberately read-only. A future command interface must be gated by
// Supervisor and verified for each exact pump/HeishaMon combination.
class PumpAdapter {
public:
    virtual ~PumpAdapter() = default;
    virtual bool poll(PumpTelemetry& output) = 0;
};
}
