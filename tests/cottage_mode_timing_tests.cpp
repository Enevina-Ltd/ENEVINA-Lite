#include "cottage/mode_timing.hpp"
#include "cottage/mpc.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace enevina::cottage;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    using enevina::PumpRoomMode;
    ObservedHeatingRequest request;
    request.observe(PumpRoomMode::Heat,21,0,false);
    const auto initial_revision=request.generation();
    request.observe(PumpRoomMode::Heat,21,1800000,true);
    check(request.mode_minutes(1800000)==30 && request.request_minutes(1800000)==30,
          "stable heating request provides continuous sampling evidence");
    request.observe(PumpRoomMode::Heat,22,1800000,true);
    check(request.mode_minutes(1800000)==30 && request.request_minutes(1800000)==0,
          "external setpoint change revokes request continuity but keeps mode dwell");
    check(request.generation()==initial_revision+1,"setpoint change advances request generation");
    request.observe(PumpRoomMode::Heat,22,1980000,false);
    check(request.mode_minutes(1980000)==0 && request.request_minutes(1980000)==0,
          "a link gap invalidates both ages even when the returned setting matches");
    ObservedModeDuration tracker;
    check(tracker.minutes(ObservedPumpMode::Off, 180000, true)==0,
          "initial unknown age is zero");
    tracker.observe(ObservedPumpMode::Off, 1000, false);
    check(tracker.minutes(ObservedPumpMode::Off, 10801000, true)==180,
          "continuous OFF for 180 min");
    tracker.observe(ObservedPumpMode::Off, 10801000, true);
    check(tracker.minutes(ObservedPumpMode::Off, 10801000, true)==180,
          "repeated OFF telemetry must not reset elapsed age");
    check(tracker.minutes(ObservedPumpMode::Off, 10801000, false)==0,
          "stale telemetry denies elapsed duration");
    check(tracker.minutes(ObservedPumpMode::Heat, 10801000, true)==0,
          "mode mismatch denies duration");
    tracker.observe(ObservedPumpMode::Heat, 10801001, true);
    check(tracker.minutes(ObservedPumpMode::Heat, 10801001, true)==0,
          "HEAT transition resets age");
    tracker.observe(ObservedPumpMode::Heat, 10861001, true);
    check(tracker.minutes(ObservedPumpMode::Heat, 10861001, true)==1,
          "HEAT duration advances");
    tracker.observe(ObservedPumpMode::Heat, 10921001, false);
    check(tracker.minutes(ObservedPumpMode::Heat, 10921001, true)==0,
          "unverified stream continuity resets age");
    tracker.observe(ObservedPumpMode::Other, 10922001, true);
    check(tracker.minutes(ObservedPumpMode::Other, 10922001, true)==0,
          "non-heat modes never treated as OFF");
    tracker.observe(ObservedPumpMode::Off, 10923001, false);
    tracker.invalidate();
    check(tracker.minutes(ObservedPumpMode::Off, 11123001, true)==0,
          "reconnection requires new continuous observation");
    tracker.observe(ObservedPumpMode::Off, 11123001, false);
    check(tracker.minutes(ObservedPumpMode::Off, 11122000, true)==0,
          "backwards monotonic timestamp fails conservative");
    check(tracker.minutes(ObservedPumpMode::Off, 999999999999ULL, true)==600,
          "long operation age bounded at 600 min");
    // Codex reproduction: OFF at 8.2C with -10C weather.
    // A false age of zero prevents immediate HEAT and makes the forecast
    // infeasible; a known 180min OFF period allows an immediate transition.
    Parameters parameters{};
    parameters.thermal_mass_kwh_per_c=2;
    parameters.loss_kw_per_c=.12;
    parameters.heat_kw_at_zero_c=3.2;
    parameters.heat_kw_change_per_outdoor_c=.03;
    parameters.minimum_heat_kw=1;
    parameters.cop_at_zero_c=2.8;
    parameters.cop_change_per_outdoor_c=.04;
    parameters.minimum_cop=1.2;
    parameters.maximum_cop=5;
    parameters.planning_min_c=8;
    parameters.critical_c=5;
    parameters.maximum_indoor_c=14.5;
    parameters.min_heat_minutes=60;
    parameters.min_off_minutes=60;
    parameters.startup_delay_minutes=3;
    std::array<Period,96> forecast{};
    for(auto& slot:forecast) slot={-10,0.1,0,1};
    check(optimize(parameters,{8.2,false,0},
          forecast.data(),forecast.size()).status==MpcStatus::Infeasible,
          "unknown dwell age prevents an unproven immediate HEAT request");
    check(optimize(parameters,{8.2,false,180},
          forecast.data(),forecast.size()).status==MpcStatus::Ok,
          "confirmed OFF dwell time permits a feasible immediate HEAT plan");
    std::puts("PASS observed mode age and reconnection safety");
}
