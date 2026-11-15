#pragma once
#include <cstddef>
#include <cstdint>

namespace enevina {
struct OutdoorPoint {
    int64_t timestamp_utc=0;
    double temperature_c=0;
};
// FMI WFS SimpleFeature temperature records, in hourly UTC order.
// Incomplete or malformed records invalidate the whole response.
bool parse_fmi_temperature(const char* xml, OutdoorPoint* output,
    size_t capacity, size_t& count);
// Linear interpolation onto 15-minute UTC slots. Requires full hourly coverage;
// never extrapolates an unobserved end of the source forecast.
bool interpolate_fmi_temperature(const OutdoorPoint* source, size_t count,
    int64_t first_utc, double* output_c, size_t slots);
}
