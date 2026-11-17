#pragma once
#include <cstdint>

namespace enevina {
// Shared by all manufacturer adapters: a network event is NOT evidence
// that the pump produced a fresh measurement. Adapter-specific code must
// validate the upstream source-time contract before setting verified=true.
enum class MeasurementQuality : uint8_t {
    Missing, UnverifiedSource, VerifiedFresh, StaleSource, InvalidClock
};
struct MeasurementEvidence {
    int64_t received_utc=0;     // When Lite received the value
    int64_t source_utc=0;       // Original, proven pump-side read timestamp
    bool source_time_verified=false;
};
constexpr MeasurementQuality assess_measurement(
    const MeasurementEvidence& e,int64_t now_utc,int64_t max_age_s) {
    if(e.received_utc<=0 || now_utc<=0 || max_age_s<=0)
        return MeasurementQuality::Missing;
    if(e.received_utc>now_utc)
        return MeasurementQuality::InvalidClock;
    if(!e.source_time_verified || e.source_utc<=0)
        return MeasurementQuality::UnverifiedSource;
    if(e.source_utc>e.received_utc)
        return MeasurementQuality::InvalidClock;
    if(now_utc-e.source_utc>max_age_s)
        return MeasurementQuality::StaleSource;
    return MeasurementQuality::VerifiedFresh;
}
constexpr bool verified_fresh(const MeasurementEvidence& e,
                              int64_t now_utc,int64_t max_age_s) {
    return assess_measurement(e,now_utc,max_age_s)==
           MeasurementQuality::VerifiedFresh;
}
constexpr const char* quality_name(MeasurementQuality q) {
    switch(q) {
    case MeasurementQuality::Missing: return "MISSING";
    case MeasurementQuality::UnverifiedSource: return "UNVERIFIED_SOURCE";
    case MeasurementQuality::VerifiedFresh: return "VERIFIED_FRESH";
    case MeasurementQuality::StaleSource: return "STALE_SOURCE";
    case MeasurementQuality::InvalidClock: return "INVALID_CLOCK";
    }
    return "UNKNOWN";
}
} // namespace enevina
