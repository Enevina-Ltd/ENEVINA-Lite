#pragma once
// Automatic, deterministic building + heat-pump identification for the
// HEAT 16 C / OFF cottage model (cottage/physics.hpp). No AI, no training
// service: a 6-parameter recursive least-squares (RLS) fit of the SAME
// discrete heat balance that advance() integrates, plus an optional
// 4-parameter electric fit from a meter the owner confirmed heat-pump-only.
//
// What is and is not identifiable from these measurements (see
// docs/MPC_CONTROL_20261120.md section 2):
//  * From indoor temperature, outdoor temperature and the HEAT/OFF request:
//    the time constant tau = C/UA, the heating lift G(To) = Q_on(To)/UA in K,
//    the start delay, the after-heat and a constant gain term. These fully
//    determine the indoor-temperature predictions the MPC needs.
//  * The ABSOLUTE heat capacity C [kWh/K], loss UA [kW/K] and COP need one
//    absolute heat-output reference (a heat meter). A heat-pump-only electric
//    meter gives the electric draw; combined with COP >= 1 it gives LOWER
//    BOUNDS for C and UA, never a COP value. This module never reports a COP.
//  * A confirmed shared meter may estimate incremental electric consumption
//    after stable OFF-load subtraction; no absolute bounds or COP follow.
//    Unconfirmed meters provide baseline diagnostics only.
//
// Calibration is "trusted" only after a walk-forward (out-of-sample) test:
// each 4-hour window is predicted open-loop with the parameters frozen at
// its start and compared with the later measurements. Nothing is assumed
// calibrated from defaults, priors or the fit's own residual alone.
#include "cottage/physics.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace enevina::cottage {

enum class CalibrationState : uint8_t {
    NoData,      // nothing learned yet (or reset)
    Learning,    // collecting excitation; parameters not yet plausible/certain
    Validating,  // plausible fit, walk-forward windows accumulating
    Calibrated,  // walk-forward error within limits: MPC may be used
    Degraded     // was calibrated, recent walk-forward windows failed
};
enum class CalibrationBlock : uint8_t {
    None, TooFewSamples, NeedHeatingSlots, NeedOffSlots, NeedSwitches,
    ParametersImplausible, ParametersUncertain, NeedValidationWindows,
    ValidationErrorHigh, ValidationBiasHigh, RecentValidationFailed,
    ValidationStale
};
const char* calibration_state_name(CalibrationState s);
const char* calibration_block_name(CalibrationBlock b);
// Diagnostic for the last completed observation, separate from the
// longer-term calibration requirements. Not stored in the model blob.
enum class LearningObservation : uint8_t {
    None, Accepted, NeedPrevious, RoomInvalid, OutdoorInvalid, RequestInvalid,
    OutsideDomain, InvalidTransition, NumericalReject, DefrostObserved
};
const char* learning_observation_name(LearningObservation state);

// One completed 15-minute UTC slot. Every flag is the caller's honest
// statement about its source; an invalid field simply skips that use.
struct SlotObservation {
    int64_t slot_utc=0;          // slot start, multiple of 900 s
    bool indoor_valid=false;     // both ends from a fresh independent sensor
    double indoor_start_c=0;
    double indoor_end_c=0;
    bool outdoor_valid=false;    // as-of forecast/measurement for this slot
    double outdoor_c=0;
    bool heat_valid=false;       // request observed throughout (constant or one bracketed transition)
    bool heat_requested=false;   // HEAT (at the model's 16 C setpoint) vs OFF
    bool in_model_domain=true;   // false: setpoint above 16 C, other modes...
    bool electric_valid=false;   // energy counter delta over the slot
    double electric_kwh=0;
    bool meter_pump_only=false;  // owner-confirmed heat-pump-only meter
    double setpoint_c=0;         // HEAT request in the variable-setpoint domain
    // -1: unchanged request for the slot. Otherwise a single continuously
    // observed mode change, with the fraction of time BEFORE the transition.
    // heat_requested is the END mode; setpoint_c is the HEAT segment target.
    // No transition is inferred from a gap or an uncertain command.
    double transition_before_fraction=-1.0;
    bool defrost_observed=false; // explicit gateway report, including protected recovery
    // Qualified whole-house increment above a learned OFF reference.
    // Never assert that this is a direct pump-only meter.
    bool meter_incremental=false;
};

template<size_t N> struct RlsState {
    double theta[N]{};
    double p[N][N]{};
    double residual_var=0;   // EW variance of a-posteriori residuals
    uint32_t updates=0;
    uint32_t outliers=0;
};

struct ValidationWindow {
    int64_t end_utc=0;
    float end_error_c=0;     // predicted - measured at the window end
    float rms_c=0;           // RMS over the window's slot ends
    float max_abs_c=0;
    uint8_t had_switch=0;
};

struct ModelEstimate {
    // Identified directly (normalised, no absolute heat scale needed).
    double tau_h=0, tau_std_h=0;            // C/UA
    double lift_k_at_0c=0;                  // steady-state rise when ON at To=0
    double lift_k_per_c=0;                  // d(rise)/d(To)
    double lift_ref_outdoor_c=0;            // middle of the observed outdoor range
    double lift_k_ref=0, lift_std_k=0;      // rise and its std at that outdoor
    double gain_rise_k=0;                   // constant internal/solar gain rise
    double start_delay_min=0, start_delay_std_min=0;
    double afterheat_k=0;                   // extra rise in the first OFF slot
    double one_step_sigma_c=0;
    double outdoor_min_c=0, outdoor_max_c=0; // range the data covers
    bool plausible=false;
    // Electric model: direct pump-only or explicitly tagged incremental estimate.
    bool electric_trusted=false;
    bool electric_incremental=false;
    double baseload_reference_w=-1;
    double electric_on_kw_at_0c=0;          // above standby, steady HEAT
    double electric_on_kw_per_c=0;
    double electric_standby_kw=0;
    double electric_start_kwh=0;
    double electric_rel_error=0;
    uint32_t electric_samples=0;
    // Absolute scale: only lower bounds (COP >= 1) with a pump-only meter.
    bool absolute_bounds=false;
    double ua_lower_kw_per_c=0, capacity_lower_kwh_per_c=0;
    bool cop_identified=false;              // always false, see header comment
};

struct CalibrationReport {
    CalibrationState state=CalibrationState::NoData;
    CalibrationBlock block=CalibrationBlock::TooFewSamples;
    uint32_t samples=0, heating_slots=0, off_slots=0, starts=0, stops=0;
    uint32_t skipped=0, outliers=0;
    uint32_t windows=0, windows_with_switch=0, windows_aborted=0;
    double validation_rms_c=0, validation_bias_c=0, validation_max_abs_c=0;
    double planning_uncertainty_c=0;        // fed to Parameters when calibrated
    int64_t last_sample_utc=0, last_window_utc=0;
    uint32_t revision=0;                    // increments on every model update
    LearningObservation last_observation=LearningObservation::None;
    uint32_t defrost_skipped_this_boot=0;
    ModelEstimate model{};
};

struct ComfortBand {
    double target_c=0;
    double lower_c=0;   // MPC comfort floor (before uncertainty margin)
    double upper_c=0;   // MPC maximum indoor temperature
};
// The owner's HARD room-temperature limits (0 = not set), exactly the
// semantics of the optimisation-profile comfort_min_c / comfort_max_c: 5..26 C
// in 0.5 C steps, both set at least 1 C apart. The MPC may be tighter than
// these, never looser.
struct ComfortLimits {
    double min_c=0;
    double max_c=0;
};
inline bool operator==(const ComfortLimits& a,const ComfortLimits& b) {
    return a.min_c==b.min_c && a.max_c==b.max_c;
}
// Are the limits well formed (finite, in range, on a 0.5 C grid, >= 1 C apart)?
bool valid_comfort_limits_for_mpc(const ComfortLimits& limits);
// Comfort band used by the MPC for a room target and the owner's hard limits;
// false when the target is outside the HEAT 16 C / OFF model domain (e.g.
// occupied 21 C) OR the limits are malformed / cannot contain the target with
// the thermostat's 0.5 C margin (the caller then falls back, never invents
// other limits). lower = max(target - 0.5, owner min), upper =
// min(target + 1.0, 15.8, owner max).
bool mpc_comfort_band(double target_c, ComfortBand& out,
                      const ComfortLimits& limits=ComfortLimits{},bool variable_setpoints=false,
                      bool maintenance_storage=false);

enum class ModelBuildError : uint8_t {
    None, NotCalibrated, Implausible, OutsideDomain, NumericalFailure
};
struct TrendPoint {
    int64_t utc=0;            // slot END
    float measured_c=0;
    float one_step_c=0;       // NaN when no prediction was possible
    float open_loop_c=0;      // walk-forward window prediction, NaN if none
};

class BuildingIdentifier {
public:
    static constexpr size_t thermal_n=6, electric_n=4;
    static constexpr size_t window_slots=16;     // 4 h walk-forward windows
    static constexpr size_t window_ring=24;
    static constexpr size_t trend_slots=96;      // last 24 h for the UI
    static constexpr uint32_t blob_version=1;

    explicit BuildingIdentifier(bool variable_setpoints=false);
    void reset();
    bool variable_setpoints() const { return variable_setpoints_; }
    // Retain the electric fit only while it belongs to the selected meter.
    // Identity zero withdraws consent for the electric fit.
    void select_electric_meter(uint32_t identity,bool incremental=false);
    // A changed stable OFF regime means the learned constant heat gain
    // must be revalidated. Do not add electrical watts to an already
    // identified normalised thermal gain or invent UA/C.
    bool note_baseload_regime(double watts);
    // Optional prior (e.g. a deliberately wrong guess in tests). Weight is the
    // inverse prior covariance scale: small = weak prior. Never marks the
    // model calibrated; only data + walk-forward validation can.
    void set_prior(double tau_h, double lift_k, double weight);
    // Feed one completed slot. Returns true when the thermal fit was updated.
    bool add(const SlotObservation& o);
    // Current report; `now_utc` lets a long-unvalidated model fall back.
    CalibrationReport report(int64_t now_utc) const;
    bool calibrated(int64_t now_utc) const {
        return report(now_utc).state==CalibrationState::Calibrated;
    }
    // MPC parameters from the identified model and a comfort band.
    // `meter_pump_only` must be the CURRENT owner confirmation; the electric
    // model is used only while it is still true. An incremental model
    // requires current qualified shared-meter provenance instead.
    ModelBuildError build_parameters(int64_t now_utc, const ComfortBand& band,
        bool meter_pump_only, double currency_scale, Parameters& out,
        bool& energy_in_kwh,bool meter_incremental=false) const;
    // Room for the UI: measured vs predicted over the last 24 h.
    size_t trend(TrendPoint* out, size_t capacity) const;

    // Fixed-size persistence image, versioned and CRC-protected. The
    // identity binds it to one pump/sensor configuration; a mismatch, a
    // corrupt image or an unsupported format is rejected. Older supported
    // images lack the meter identity and can restore only the thermal fit.
    static constexpr size_t blob_capacity=2048;
    bool save(uint32_t identity, uint8_t* out, size_t capacity, size_t& used) const;
    bool load(uint32_t identity, const uint8_t* in, size_t size);

private:
    ModelEstimate estimate() const;
    void validation_step(const SlotObservation& o,bool start,bool stop);
    double heat_demand(const SlotObservation& o,double indoor_c) const;
    double requested_heat_fraction(const SlotObservation& o,double indoor_c) const;
    void push_trend(int64_t utc, double measured, double one_step, double open_loop);
    void refresh_state();

    bool variable_setpoints_=false;
    RlsState<thermal_n> thermal_{};
    RlsState<electric_n> electric_{};
    uint32_t electric_meter_identity_=0;
    bool electric_incremental_=false;
    double baseload_reference_w_=-1;
    // Continuity of the HEAT/OFF request for start/stop regressors.
    int64_t prev_slot_utc_=0;
    int8_t prev_heat_=-1;
    // Excitation and range statistics.
    uint32_t samples_=0, heating_=0, off_=0, starts_=0, stops_=0, skipped_=0;
    double outdoor_min_=1e9, outdoor_max_=-1e9;
    double start_outdoor_sum_=0, stop_outdoor_sum_=0;
    double elec_err_sq_=0, elec_sq_=0;
    uint32_t elec_on_=0;
    // Walk-forward validation.
    bool learning_ok_=false;
    bool window_active_=false;
    int64_t window_next_utc_=0;
    double window_theta_[thermal_n]{};
    double window_pred_=0, window_sq_=0, window_max_=0;
    uint32_t window_count_=0;
    uint8_t window_switch_=0;
    std::array<ValidationWindow,window_ring> ring_{};
    uint32_t ring_count_=0, ring_head_=0, windows_aborted_=0;
    bool was_calibrated_=false;
    CalibrationState state_=CalibrationState::NoData;
    CalibrationBlock block_=CalibrationBlock::TooFewSamples;
    int64_t last_sample_utc_=0;
    uint32_t revision_=0;
    LearningObservation last_observation_=LearningObservation::None;
    uint32_t defrost_skipped_this_boot_=0;
    // Display-only, not persisted.
    std::array<TrendPoint,trend_slots> trend_{};
    uint32_t trend_count_=0, trend_head_=0;
};

// Explicit numeric limits (engineering choices, documented; not field-proven).
struct CalibrationLimits {
    static constexpr uint32_t min_samples=192;       // 2 days of valid slots
    static constexpr uint32_t min_heating=32, min_off=32;
    static constexpr uint32_t min_starts=4, min_stops=4;
    static constexpr double max_tau_rel_std=0.30;
    static constexpr double max_lift_rel_std=0.20;
    static constexpr double min_tau_h=2.0, max_tau_h=500.0;
    static constexpr double min_lift_k=0.5, max_lift_k=80.0;
    static constexpr uint32_t windows_needed=12;     // 2 days of 4 h windows
    static constexpr uint32_t switch_windows_needed=4;
    static constexpr double max_validation_rms_c=0.50;
    static constexpr double max_validation_bias_c=0.25;
    static constexpr double max_validation_abs_c=1.50;
    static constexpr uint32_t degrade_windows=4;
    static constexpr double degrade_rms_c=1.00;
    static constexpr int64_t stale_validation_s=14*24*3600;
    static constexpr double outdoor_margin_c=3.0;    // extrapolation allowed
};

} // namespace enevina::cottage
