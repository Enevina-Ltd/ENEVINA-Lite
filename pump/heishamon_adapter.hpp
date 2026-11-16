#pragma once
#include "pump/pump_adapter.hpp"

namespace enevina {
// Transport returns a numeric Value from HeishaMon's /json?topic=... response.
class TopicReader {
public:
    virtual ~TopicReader() = default;
    virtual bool read_number(const char* topic, double& value) = 0;
};

class HeishaMonAdapter final : public PumpAdapter {
public:
    explicit HeishaMonAdapter(TopicReader& transport): transport_(transport) {}
    bool poll(PumpTelemetry& output) override;
private:
    TopicReader& transport_;
};
}
