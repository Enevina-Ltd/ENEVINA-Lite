#include "pump/pump_adapter.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
using namespace enevina;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL: %s\n",#x); std::exit(1); } } while(false)

class UnknownAdapter final : public PumpAdapter {
public:
    bool poll(PumpTelemetry&) override { return false; }
};
class TestPump final : public PumpAdapter {
public:
    PumpDescriptor descriptor() const noexcept override {
        PumpCapabilities c{};
        c.reports_indoor_temperature=true;
        c.reports_outdoor_temperature=true;
        c.reports_compressor_frequency=true;
        // No model-specific write has been tested; remain false.
        return {"Test manufacturer","Test model",HeatPumpType::AirToAir,c};
    }
    bool poll(PumpTelemetry& v) override {
        v.operating_mode=1;
        return true;
    }
};
int main() {
    UnknownAdapter unknown;
    auto u=unknown.descriptor();
    CHECK(u.manufacturer==nullptr && u.model==nullptr);
    CHECK(u.type==HeatPumpType::Unknown);
    CHECK(!u.capabilities.reports_compressor_frequency);
    CHECK(!u.capabilities.heating_setpoint_write_verified);
    CHECK(!u.capabilities.dhw_setpoint_write_verified);
    CHECK(!u.capabilities.pump_side_freshness_verified);
    TestPump test;
    const auto d=test.descriptor();
    CHECK(std::strcmp(d.manufacturer,"Test manufacturer")==0);
    CHECK(std::strcmp(d.model,"Test model")==0);
    CHECK(d.type==HeatPumpType::AirToAir);
    CHECK(d.capabilities.reports_indoor_temperature);
    CHECK(d.capabilities.reports_outdoor_temperature);
    CHECK(d.capabilities.reports_compressor_frequency);
    CHECK(!d.capabilities.reports_defrost);
    CHECK(!d.capabilities.reports_dhw_temperature);
    CHECK(!d.capabilities.heating_setpoint_write_verified);
    CHECK(!d.capabilities.off_command_verified);
    PumpTelemetry sample{};
    CHECK(test.poll(sample) && sample.operating_mode==1);
    // Both independent PRs must coexist in the SAME PumpTelemetry type.
    constexpr int64_t now=1800000000;
    CHECK(assess_measurement(sample.evidence,now,180)==MeasurementQuality::Missing);
    sample.evidence={now,0,false}; // transport event alone
    CHECK(assess_measurement(sample.evidence,now,180)==
          MeasurementQuality::UnverifiedSource);
    sample.evidence={now,now,true}; // independently proven source timestamp
    CHECK(verified_fresh(sample.evidence,now,180));
    std::puts("PASS unknown defaults and explicit manufacturer capabilities");
}
