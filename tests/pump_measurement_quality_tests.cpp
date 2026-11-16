#include "pump/measurement_quality.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
using namespace enevina;
#define CHECK(expr) do { if(!(expr)) { \
    std::fprintf(stderr,"FAIL %s:%d: %s\\n",__FILE__,__LINE__,#expr); \
    std::exit(1); \
} } while(false)
int main() {
    constexpr int64_t now=1800000000;
    CHECK(assess_measurement({},now,180)==MeasurementQuality::Missing);
    // A heartbeat and SSE event may arrive now while the last pump
    // serial read is unknown; transport activity MUST NOT renew freshness.
    CHECK(assess_measurement({now,0,false},now,180)==
          MeasurementQuality::UnverifiedSource);
    CHECK(!verified_fresh({now,now,false},now,180));
    CHECK(verified_fresh({now,now,true},now,180));
    CHECK(verified_fresh({now-90,now-90,true},now,180));
    CHECK(verified_fresh({now-180,now-180,true},now,180));
    CHECK(assess_measurement({now-181,now-181,true},now,180)==
          MeasurementQuality::StaleSource);
    // Replayed source sample through a fresh websocket/HTTP connection.
    CHECK(assess_measurement({now,now-300,true},now,180)==
          MeasurementQuality::StaleSource);
    CHECK(assess_measurement({now+1,now+1,true},now,180)==
          MeasurementQuality::InvalidClock);
    CHECK(assess_measurement({now-1,now,true},now,180)==
          MeasurementQuality::InvalidClock);
    CHECK(assess_measurement({now,now,true},now,0)==
          MeasurementQuality::Missing);
    CHECK(std::strcmp(quality_name(MeasurementQuality::UnverifiedSource),
                      "UNVERIFIED_SOURCE")==0);
    std::puts("PASS manufacturer-independent measurement provenance");
}
