#pragma once
// Asynchronous city search for the first-start wizard and weather location.
// One worker task owns the single outbound HTTPS request; the web server only
// starts a job and polls its state, so it is never blocked by the network.
#include "forecast/geocode.hpp"
#include <cstddef>
#include <cstdint>

namespace enevina_firmware {
enum class GeocodeState : uint8_t { Idle=0, Pending=1, Done=2, Failed=3 };
// Failure causes visible to the UI (never a silent empty list).
enum class GeocodeError : uint8_t {
    None=0, BadQuery=1, Busy=2, Offline=3, ServerError=4, BadResponse=5, LowMemory=6
};
constexpr size_t kGeocodeMaxResults=8;
struct GeocodeSnapshot {
    GeocodeState state=GeocodeState::Idle;
    GeocodeError error=GeocodeError::None;
    size_t count=0;
    enevina::GeoPlace places[kGeocodeMaxResults]{};
    char query[enevina::kGeoMaxQueryBytes+1]{};
    char country[3]{};
    char language[3]{};
};
// Starts (or re-uses) a search. Returns the state of the job for THIS query:
//  Pending - running, poll again;  Done/Failed - result in `out`;
//  Failed+Busy - a different search is still running.
// The same query within 120 s is answered from the last result (no new
// request); at most one new request is started per 2 seconds.
GeocodeState geocode_search(const char* query,const char* country,
                            const char* language,GeocodeSnapshot& out);
}
