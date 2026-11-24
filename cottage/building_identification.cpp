#include "cottage/building_identification.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

namespace enevina::cottage {
// Named (not anonymous): firmware/cottage/src/core.cpp is a unity build.
namespace identification_detail {
constexpr double thermal_lambda=0.9995;   // ~21-day memory at 15-min slots
constexpr double electric_lambda=0.999;   // ~10 days
constexpr double prior_p=10.0;
constexpr double max_trace=1.0e5;
constexpr double nan=std::numeric_limits<double>::quiet_NaN();
bool id_finite(double x) {return std::isfinite(x);}

template<size_t N> void rls_init(RlsState<N>& r,const double* theta0,double p0) {
    r=RlsState<N>{};
    for(size_t i=0;i<N;++i) {
        r.theta[i]=theta0?theta0[i]:0.0;
        for(size_t j=0;j<N;++j) r.p[i][j]=i==j?p0:0.0;
    }
}
template<size_t N> double rls_dot(const RlsState<N>& r,const double* phi) {
    double s=0;
    for(size_t i=0;i<N;++i) s+=r.theta[i]*phi[i];
    return s;
}
// Standard exponentially-weighted RLS with symmetrisation and a covariance
// trace cap (anti-windup when a direction is not excited for days).
template<size_t N> bool rls_update(RlsState<N>& r,const double* phi,double y,
                                   double lambda,bool reject_outliers) {
    double pphi[N]{};
    for(size_t i=0;i<N;++i) for(size_t j=0;j<N;++j) pphi[i]+=r.p[i][j]*phi[j];
    double denom=lambda;
    for(size_t i=0;i<N;++i) denom+=phi[i]*pphi[i];
    if(!id_finite(denom) || denom<=0) return false;
    const double e=y-rls_dot(r,phi);
    if(reject_outliers && r.updates>=96 && r.residual_var>0) {
        const double sigma=std::sqrt(r.residual_var*(1.0+(denom-lambda)));
        if(std::fabs(e)>std::max(0.6,6.0*sigma)) {r.outliers++;return false;}
    }
    double k[N];
    for(size_t i=0;i<N;++i) k[i]=pphi[i]/denom;
    double theta[N];
    for(size_t i=0;i<N;++i) theta[i]=r.theta[i]+k[i]*e;
    double p[N][N];
    double trace=0;
    for(size_t i=0;i<N;++i) for(size_t j=0;j<N;++j)
        p[i][j]=(r.p[i][j]-k[i]*pphi[j])/lambda;
    for(size_t i=0;i<N;++i) for(size_t j=i+1;j<N;++j) {
        const double m=0.5*(p[i][j]+p[j][i]);p[i][j]=p[j][i]=m;
    }
    for(size_t i=0;i<N;++i) {
        if(!(p[i][i]>0) || !id_finite(p[i][i]) || !id_finite(theta[i])) return false;
        trace+=p[i][i];
    }
    const double scale=trace>max_trace?max_trace/trace:1.0;
    for(size_t i=0;i<N;++i) {
        r.theta[i]=theta[i];
        for(size_t j=0;j<N;++j) r.p[i][j]=p[i][j]*scale;
    }
    const double post=y-rls_dot(r,phi);
    r.updates++;
    const double alpha=std::max(1.0/r.updates,0.01);
    r.residual_var=(1.0-alpha)*r.residual_var+alpha*post*post;
    return true;
}
uint32_t id_crc32(const uint8_t* d,size_t n) {
    uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;++i) {
        c^=d[i];
        for(int k=0;k<8;++k) c=(c>>1)^(0xEDB88320u&(0u-(c&1u)));
    }
    return ~c;
}
} // namespace identification_detail
using namespace identification_detail;

const char* calibration_state_name(CalibrationState s) {
    switch(s) {
    case CalibrationState::NoData:return "NO_DATA";
    case CalibrationState::Learning:return "LEARNING";
    case CalibrationState::Validating:return "VALIDATING";
    case CalibrationState::Calibrated:return "CALIBRATED";
    case CalibrationState::Degraded:return "DEGRADED";
    }
    return "UNKNOWN";
}
const char* calibration_block_name(CalibrationBlock b) {
    switch(b) {
    case CalibrationBlock::None:return "NONE";
    case CalibrationBlock::TooFewSamples:return "TOO_FEW_SAMPLES";
    case CalibrationBlock::NeedHeatingSlots:return "NEED_HEATING_SLOTS";
    case CalibrationBlock::NeedOffSlots:return "NEED_OFF_SLOTS";
    case CalibrationBlock::NeedSwitches:return "NEED_SWITCHES";
    case CalibrationBlock::ParametersImplausible:return "PARAMETERS_IMPLAUSIBLE";
    case CalibrationBlock::ParametersUncertain:return "PARAMETERS_UNCERTAIN";
    case CalibrationBlock::NeedValidationWindows:return "NEED_VALIDATION_WINDOWS";
    case CalibrationBlock::ValidationErrorHigh:return "VALIDATION_ERROR_HIGH";
    case CalibrationBlock::ValidationBiasHigh:return "VALIDATION_BIAS_HIGH";
    case CalibrationBlock::RecentValidationFailed:return "RECENT_VALIDATION_FAILED";
    case CalibrationBlock::ValidationStale:return "VALIDATION_STALE";
    }
    return "UNKNOWN";
}

bool valid_comfort_limits_for_mpc(const ComfortLimits& l) {
    const auto ok=[](double v){
        return v==0 || (id_finite(v) && v>=5.0 && v<=26.0 &&
                        std::fabs(v*2.0-std::round(v*2.0))<1e-9);
    };
    if(!ok(l.min_c) || !ok(l.max_c)) return false;
    return l.min_c==0 || l.max_c==0 || l.max_c-l.min_c>=1.0;
}

bool mpc_comfort_band(double target_c,ComfortBand& out,const ComfortLimits& limits,
                      bool variable_setpoints) {
    out=ComfortBand{};
    if(!id_finite(target_c) || !valid_comfort_limits_for_mpc(limits) ||
       (variable_setpoints && target_c>26)) return false;
    // Same lower edge as the thermostat/price controller (target - 0.5).
    // HEAT 16 C / OFF physics are only valid up to 15.8 C indoors.
    double lower=std::max(5.0,target_c-0.5);
    double upper=variable_setpoints?target_c+0.5:std::min(15.8,target_c+1.0);
    // The owner's limits are HARD: they can only tighten the band. A limit
    // that is not at least 0.5 C beyond the target on its own side (the
    // thermostat deadband) is contradictory, not a limit to be ignored.
    if(limits.min_c>0) {
        if(limits.min_c>target_c-0.5+1e-9) return false;
        lower=variable_setpoints?limits.min_c:std::max(lower,limits.min_c);
    }
    if(limits.max_c>0) {
        if(limits.max_c<target_c+0.5-1e-9) return false;
        upper=variable_setpoints?limits.max_c:std::min(upper,limits.max_c);
    }
    if(target_c<5.0 || upper-lower<0.8) return false;
    out.target_c=target_c;out.lower_c=lower;out.upper_c=upper;
    return true;
}

BuildingIdentifier::BuildingIdentifier(bool variable_setpoints)
    : variable_setpoints_(variable_setpoints) {
    // Members are initialized directly at their destination (PSRAM on ESP).
    // Weak generic prior: 30 h time constant, no heating lift known.
    const double thermal0[thermal_n]={std::exp(-slot_hours/30.0),0,0,0,0,0};
    rls_init(thermal_,thermal0,prior_p);
    rls_init(electric_,nullptr,prior_p);
}

void BuildingIdentifier::reset() {
    // Do not create a full BuildingIdentifier temporary on the task stack.
    // In-place reconstruction also clears all transient validation/trend data.
    const bool variable=variable_setpoints_;
    this->~BuildingIdentifier();
    new(this) BuildingIdentifier(variable);
}

void BuildingIdentifier::select_electric_meter(uint32_t identity) {
    if(identity==electric_meter_identity_)return;
    electric_meter_identity_=identity;
    rls_init(electric_,nullptr,prior_p);
    elec_err_sq_=0;elec_sq_=0;elec_on_=0;
    revision_++;
}

void BuildingIdentifier::set_prior(double tau_h,double lift_k,double weight) {
    if(!id_finite(tau_h) || tau_h<=0.5 || !id_finite(lift_k) || !id_finite(weight) || weight<=0)
        return;
    const double a=std::exp(-slot_hours/tau_h);
    thermal_.theta[0]=a;
    thermal_.theta[1]=lift_k*(1.0-a);
    const double p=std::clamp(1.0/weight,1e-6,prior_p);
    for(size_t i=0;i<2;++i) thermal_.p[i][i]=p;
}

void BuildingIdentifier::push_trend(int64_t utc,double measured,double one_step,double open_loop) {
    TrendPoint t{};
    t.utc=utc;
    t.measured_c=static_cast<float>(measured);
    t.one_step_c=static_cast<float>(one_step);
    t.open_loop_c=static_cast<float>(open_loop);
    trend_[trend_head_]=t;
    trend_head_=(trend_head_+1)%trend_slots;
    trend_count_=std::min<uint32_t>(trend_count_+1,trend_slots);
}

size_t BuildingIdentifier::trend(TrendPoint* out,size_t capacity) const {
    if(!out) return 0;
    const size_t n=std::min<size_t>(trend_count_,capacity);
    const size_t first=(trend_head_+trend_slots-trend_count_)%trend_slots;
    const size_t skip=trend_count_-n;
    for(size_t i=0;i<n;++i) out[i]=trend_[(first+skip+i)%trend_slots];
    return n;
}

namespace {
constexpr double residential_thermostat_band_c=1.0;
}
double BuildingIdentifier::heat_demand(const SlotObservation& o,double indoor_c) const {
    return variable_setpoints_?
        std::clamp((o.setpoint_c-indoor_c)/residential_thermostat_band_c,0.0,1.0):1.0;
}
double BuildingIdentifier::requested_heat_fraction(const SlotObservation& o,double indoor_c) const {
    const double f=o.transition_before_fraction;
    const double duty=f<0?(o.heat_requested?1.0:0.0):(o.heat_requested?1.0-f:f);
    return duty>0?duty*heat_demand(o,indoor_c):0.0;
}

void BuildingIdentifier::validation_step(const SlotObservation& o,bool start,bool stop) {
    if(!learning_ok_) {window_active_=false;return;}
    if(window_active_ && o.slot_utc!=window_next_utc_) {
        window_active_=false;windows_aborted_++;
    }
    if(!window_active_) {
        std::memcpy(window_theta_,thermal_.theta,sizeof(window_theta_));
        window_pred_=o.indoor_start_c;
        window_sq_=0;window_max_=0;window_count_=0;window_switch_=0;
        window_next_utc_=o.slot_utc;
        window_active_=true;
    }
    const double to=o.outdoor_c,u=requested_heat_fraction(o,window_pred_);
    const double* t=window_theta_;
    window_pred_=to+t[0]*(window_pred_-to)+t[1]*u+t[2]*u*to+
        t[3]*(start?heat_demand(o,window_pred_):0.0)+t[4]*(stop?1.0:0.0)+t[5];
    const double err=window_pred_-o.indoor_end_c;
    window_sq_+=err*err;
    window_max_=std::max(window_max_,std::fabs(err));
    window_count_++;
    if(start||stop) window_switch_=1;
    window_next_utc_+=static_cast<int64_t>(slot_minutes)*60;
    if(window_count_>=window_slots) {
        ValidationWindow w{};
        w.end_utc=o.slot_utc+900;
        w.end_error_c=static_cast<float>(err);
        w.rms_c=static_cast<float>(std::sqrt(window_sq_/window_count_));
        w.max_abs_c=static_cast<float>(window_max_);
        w.had_switch=window_switch_;
        ring_[ring_head_]=w;
        ring_head_=(ring_head_+1)%window_ring;
        ring_count_=std::min<uint32_t>(ring_count_+1,window_ring);
        window_active_=false;
    }
}

bool BuildingIdentifier::add(const SlotObservation& o) {
    if(o.slot_utc<=0 || o.slot_utc%900!=0 ||
       (last_sample_utc_>0 && o.slot_utc<=last_sample_utc_))
        return false; // duplicate, replay or clock step backwards
    const bool contiguous=prev_slot_utc_>0 && prev_slot_utc_==o.slot_utc-900 &&
        prev_heat_>=0;
    const bool heat=o.heat_valid && o.heat_requested;
    const double f=o.transition_before_fraction;
    const bool transition=f>0 && f<1;
    const bool valid_fraction=f==-1.0 || transition;
    // A mixed slot is only usable when its beginning matches the preceding
    // accepted end mode. An unobserved extra boundary change is not counted.
    const bool transition_contiguous=!transition || (contiguous && prev_heat_==(heat?0:1));
    const bool start=contiguous && o.heat_valid && heat && prev_heat_==0;
    const bool stop=contiguous && o.heat_valid && !heat && prev_heat_==1;
    const bool contains_heat=heat || transition;
    const bool known_setpoint=!variable_setpoints_ || !contains_heat ||
        (id_finite(o.setpoint_c) && o.setpoint_c>=5.0 && o.setpoint_c<=35.0);
    const bool slot_ok=valid_fraction && known_setpoint && o.heat_valid && o.indoor_valid &&
        o.outdoor_valid && o.in_model_domain &&
        id_finite(o.indoor_start_c) && id_finite(o.indoor_end_c) && id_finite(o.outdoor_c) &&
        o.indoor_start_c>-10 && o.indoor_start_c<40 &&
        o.indoor_end_c>-10 && o.indoor_end_c<40 &&
        o.outdoor_c>=-55 && o.outdoor_c<=45 &&
        std::fabs(o.indoor_end_c-o.indoor_start_c)<3.0 &&
        // The pump's own 16 C thermostat throttles above ~15.5 C indoors.
        (variable_setpoints_ || !(contains_heat && o.indoor_start_c>15.3));
    const bool thermal_ok=contiguous && slot_ok && transition_contiguous;
    last_sample_utc_=o.slot_utc;
    prev_slot_utc_=o.slot_utc;
    // A start/stop is only ever inferred between two ACCEPTED slots: a slot
    // the model rejected (HEAT above 16 C, missing indoor/outdoor/pump data,
    // implausible step) breaks the chain, so the next slot is never counted
    // as the stop/start of a heating run the model did not observe
    // (Codex P2, PR #63). Learning resumes with the next accepted pair.
    prev_heat_=slot_ok?(heat?1:0):-1;
    bool updated=false;
    if(thermal_ok) {
        const double to=o.outdoor_c,u=requested_heat_fraction(o,o.indoor_start_c);
        const double phi[thermal_n]={o.indoor_start_c-to,u,u*to,
            start?heat_demand(o,o.indoor_start_c):0.0,stop?1.0:0.0,1.0};
        const double y=o.indoor_end_c-to;
        const double one_step=samples_>0?to+rls_dot(thermal_,phi):nan;
        validation_step(o,start,stop);
        const double open_loop=window_active_||window_count_>=window_slots?
            window_pred_:nan;
        updated=rls_update(thermal_,phi,y,thermal_lambda,true);
        push_trend(o.slot_utc+900,o.indoor_end_c,one_step,learning_ok_?open_loop:nan);
        if(updated) {
            samples_++;
            if(u>0.05) heating_++;
            else if(!contains_heat) off_++;
            if(start) {starts_++;start_outdoor_sum_+=to;}
            if(stop) {stops_++;stop_outdoor_sum_+=to;}
            outdoor_min_=std::min(outdoor_min_,to);
            outdoor_max_=std::max(outdoor_max_,to);
            revision_++;
        } else skipped_++;
    } else {
        if(window_active_) {window_active_=false;windows_aborted_++;}
        skipped_++;
        if(o.indoor_valid && id_finite(o.indoor_end_c))
            push_trend(o.slot_utc+900,o.indoor_end_c,nan,nan);
    }
    // Electric fit: ONLY an owner-confirmed heat-pump-only meter.
    if(o.meter_pump_only && o.electric_valid && contiguous && o.heat_valid &&
       valid_fraction && transition_contiguous && (!transition || slot_ok) &&
       (!variable_setpoints_ || slot_ok) &&
       o.outdoor_valid && o.in_model_domain && id_finite(o.electric_kwh) &&
       o.electric_kwh>=0 && o.electric_kwh<=5.0 && id_finite(o.outdoor_c)) {
        const double to=o.outdoor_c,u=requested_heat_fraction(o,o.indoor_start_c);
        const double phi[electric_n]={u,u*to,start?heat_demand(o,o.indoor_start_c):0.0,1.0};
        const double y=o.electric_kwh/slot_hours;
        if(electric_.updates>=48) {
            const double e=y-rls_dot(electric_,phi);
            elec_err_sq_+=e*e;elec_sq_+=y*y;
        }
        if(rls_update(electric_,phi,y,electric_lambda,true) && u>0.05) elec_on_++;
    }
    refresh_state();
    return updated;
}

ModelEstimate BuildingIdentifier::estimate() const {
    ModelEstimate m{};
    m.outdoor_min_c=samples_?outdoor_min_:0;
    m.outdoor_max_c=samples_?outdoor_max_:0;
    const double* t=thermal_.theta;
    const double s2=thermal_.residual_var;
    const auto cov=[&](size_t i,size_t j){return s2*thermal_.p[i][j];};
    m.one_step_sigma_c=std::sqrt(std::max(0.0,s2));
    const double a=t[0];
    if(!(a>0.0 && a<0.99999) || !samples_) return m;
    const double la=std::log(a);
    m.tau_h=-slot_hours/la;
    const double dtau=slot_hours/(a*la*la);
    m.tau_std_h=std::sqrt(std::max(0.0,dtau*dtau*cov(0,0)));
    const double om=1.0-a;
    m.lift_k_at_0c=t[1]/om;
    m.lift_k_per_c=t[2]/om;
    // Uncertainty at the middle of the observed outdoor range: u and u*To
    // are collinear over a narrow range, so G(0) alone can look uncertain
    // although the predictions inside the range are well determined.
    const double tm=0.5*(m.outdoor_min_c+m.outdoor_max_c);
    m.lift_ref_outdoor_c=tm;
    m.lift_k_ref=(t[1]+t[2]*tm)/om;
    const double ga=(t[1]+t[2]*tm)/(om*om),g1=1.0/om,g2=tm/om;
    const double lv=ga*ga*cov(0,0)+g1*g1*cov(1,1)+g2*g2*cov(2,2)+
        2*(ga*g1*cov(0,1)+ga*g2*cov(0,2)+g1*g2*cov(1,2));
    m.lift_std_k=std::sqrt(std::max(0.0,lv));
    m.gain_rise_k=t[5]/om;
    const double tos=starts_?start_outdoor_sum_/starts_:0.0;
    const double ls=t[1]+t[2]*tos;
    if(ls>1e-6) {
        const double smin=static_cast<double>(slot_minutes);
        m.start_delay_min=std::clamp(-t[3]/ls*smin,0.0,14.0);
        const double d1=t[3]/(ls*ls)*smin,d2=d1*tos,d3=-smin/ls;
        const double v=d1*d1*cov(1,1)+d2*d2*cov(2,2)+d3*d3*cov(3,3)+
            2*(d1*d2*cov(1,2)+d1*d3*cov(1,3)+d2*d3*cov(2,3));
        m.start_delay_std_min=std::sqrt(std::max(0.0,v));
    }
    m.afterheat_k=std::max(0.0,t[4]);
    const auto lift=[&](double to){return (t[1]+t[2]*to)/om;};
    const double lo=lift(m.outdoor_min_c),hi=lift(m.outdoor_max_c);
    m.plausible=m.tau_h>=CalibrationLimits::min_tau_h && m.tau_h<=CalibrationLimits::max_tau_h &&
        std::min(lo,hi)>=CalibrationLimits::min_lift_k &&
        std::max(lo,hi)<=CalibrationLimits::max_lift_k && id_finite(m.gain_rise_k) &&
        std::fabs(m.gain_rise_k)<40.0;
    // Electric model.
    const double* e=electric_.theta;
    m.electric_samples=electric_.updates;
    if(electric_.updates>=48) {
        m.electric_on_kw_at_0c=e[0];
        m.electric_on_kw_per_c=e[1];
        m.electric_start_kwh=std::max(0.0,e[2]*slot_hours);
        m.electric_standby_kw=std::max(0.0,e[3]);
        m.electric_rel_error=elec_sq_>0?std::sqrt(elec_err_sq_/elec_sq_):1.0;
        const double plo=e[0]+e[1]*m.outdoor_min_c,phi=e[0]+e[1]*m.outdoor_max_c;
        m.electric_trusted=electric_.updates>=96 && elec_on_>=32 &&
            m.electric_rel_error<=0.25 && std::min(plo,phi)>0.05 && std::max(plo,phi)<10.0;
    }
    if(m.electric_trusted && m.plausible) {
        // COP >= 1 => Q(To) >= P(To) => UA >= P(To)/G(To) for every To.
        double ua=0;
        for(int i=0;i<=10;++i) {
            const double to=m.outdoor_min_c+(m.outdoor_max_c-m.outdoor_min_c)*i/10.0;
            const double g=lift(to),p=e[0]+e[1]*to;
            if(g>0) ua=std::max(ua,p/g);
        }
        if(ua>0) {
            m.absolute_bounds=true;
            m.ua_lower_kw_per_c=ua;
            m.capacity_lower_kwh_per_c=ua*m.tau_h;
        }
    }
    m.cop_identified=false;
    return m;
}

void BuildingIdentifier::refresh_state() {
    const ModelEstimate m=estimate();
    CalibrationBlock learning=CalibrationBlock::None;
    if(samples_<CalibrationLimits::min_samples) learning=CalibrationBlock::TooFewSamples;
    else if(heating_<CalibrationLimits::min_heating) learning=CalibrationBlock::NeedHeatingSlots;
    else if(off_<CalibrationLimits::min_off) learning=CalibrationBlock::NeedOffSlots;
    else if(starts_<CalibrationLimits::min_starts || stops_<CalibrationLimits::min_stops)
        learning=CalibrationBlock::NeedSwitches;
    else if(!m.plausible) learning=CalibrationBlock::ParametersImplausible;
    else if(m.tau_std_h>CalibrationLimits::max_tau_rel_std*m.tau_h ||
            m.lift_std_k>CalibrationLimits::max_lift_rel_std*std::fabs(m.lift_k_ref))
        learning=CalibrationBlock::ParametersUncertain;
    const bool ok=learning==CalibrationBlock::None;
    if(learning_ok_ && !ok) {
        // A model that is no longer plausible invalidates its validation.
        ring_count_=0;ring_head_=0;window_active_=false;was_calibrated_=false;
    }
    learning_ok_=ok;
    if(samples_==0) {state_=CalibrationState::NoData;block_=CalibrationBlock::TooFewSamples;return;}
    if(!ok) {state_=CalibrationState::Learning;block_=learning;return;}
    const uint32_t k=std::min<uint32_t>(ring_count_,CalibrationLimits::windows_needed);
    if(k<CalibrationLimits::windows_needed) {
        state_=was_calibrated_?CalibrationState::Degraded:CalibrationState::Validating;
        block_=CalibrationBlock::NeedValidationWindows;return;
    }
    double sq=0,bias=0,maxabs=0;uint32_t switches=0;
    double recent_sq=0;
    for(uint32_t i=0;i<k;++i) {
        const auto& w=ring_[(ring_head_+window_ring-1-i)%window_ring];
        sq+=double(w.rms_c)*w.rms_c;bias+=w.end_error_c;
        maxabs=std::max(maxabs,double(w.max_abs_c));switches+=w.had_switch;
        if(i<CalibrationLimits::degrade_windows) recent_sq+=double(w.rms_c)*w.rms_c;
    }
    const double rms=std::sqrt(sq/k),mean=bias/k;
    const double recent=std::sqrt(recent_sq/CalibrationLimits::degrade_windows);
    CalibrationBlock fail=CalibrationBlock::None;
    if(recent>CalibrationLimits::degrade_rms_c) fail=CalibrationBlock::RecentValidationFailed;
    else if(rms>CalibrationLimits::max_validation_rms_c ||
            maxabs>CalibrationLimits::max_validation_abs_c)
        fail=CalibrationBlock::ValidationErrorHigh;
    else if(std::fabs(mean)>CalibrationLimits::max_validation_bias_c)
        fail=CalibrationBlock::ValidationBiasHigh;
    else if(switches<CalibrationLimits::switch_windows_needed)
        fail=CalibrationBlock::NeedValidationWindows;
    if(fail==CalibrationBlock::None) {
        state_=CalibrationState::Calibrated;block_=CalibrationBlock::None;
        was_calibrated_=true;
    } else {
        state_=was_calibrated_?CalibrationState::Degraded:CalibrationState::Validating;
        block_=fail;
    }
}

CalibrationReport BuildingIdentifier::report(int64_t now_utc) const {
    CalibrationReport r{};
    r.state=state_;r.block=block_;
    r.samples=samples_;r.heating_slots=heating_;r.off_slots=off_;
    r.starts=starts_;r.stops=stops_;r.skipped=skipped_;
    r.outliers=thermal_.outliers;
    r.windows=ring_count_;r.windows_aborted=windows_aborted_;
    r.last_sample_utc=last_sample_utc_;r.revision=revision_;
    r.model=estimate();
    const uint32_t k=std::min<uint32_t>(ring_count_,CalibrationLimits::windows_needed);
    if(k) {
        double sq=0,bias=0,maxabs=0;
        for(uint32_t i=0;i<k;++i) {
            const auto& w=ring_[(ring_head_+window_ring-1-i)%window_ring];
            sq+=double(w.rms_c)*w.rms_c;bias+=w.end_error_c;
            maxabs=std::max(maxabs,double(w.max_abs_c));
            r.windows_with_switch+=w.had_switch;
        }
        r.validation_rms_c=std::sqrt(sq/k);
        r.validation_bias_c=bias/k;
        r.validation_max_abs_c=maxabs;
        r.last_window_utc=ring_[(ring_head_+window_ring-1)%window_ring].end_utc;
    }
    r.planning_uncertainty_c=std::clamp(2.0*r.validation_rms_c,0.2,2.0);
    if(r.state==CalibrationState::Calibrated &&
       (now_utc<r.last_window_utc ||
        now_utc-r.last_window_utc>CalibrationLimits::stale_validation_s)) {
        r.state=CalibrationState::Validating;
        r.block=CalibrationBlock::ValidationStale;
    }
    return r;
}

ModelBuildError BuildingIdentifier::build_parameters(int64_t now_utc,
    const ComfortBand& band,bool meter_pump_only,double currency_scale,
    Parameters& out,bool& energy_in_kwh) const {
    out=Parameters{};
    energy_in_kwh=false;
    const CalibrationReport r=report(now_utc);
    if(r.state!=CalibrationState::Calibrated) return ModelBuildError::NotCalibrated;
    const ModelEstimate& m=r.model;
    if(!m.plausible) return ModelBuildError::Implausible;
    if(!id_finite(band.lower_c) || !id_finite(band.upper_c) ||
       band.upper_c<=band.lower_c+r.planning_uncertainty_c+0.1 ||
       band.upper_c>(variable_setpoints_?27.0:15.8) ||
       band.lower_c<5.0)
        return ModelBuildError::OutsideDomain;
    if(!id_finite(currency_scale) || currency_scale<1 || currency_scale>12)
        return ModelBuildError::NumericalFailure;
    const double a=thermal_.theta[0],om=1.0-a;
    const double lo=std::max(-55.0,m.outdoor_min_c-CalibrationLimits::outdoor_margin_c);
    const double hi=std::min(45.0,m.outdoor_max_c+CalibrationLimits::outdoor_margin_c);
    const auto lift=[&](double to){return (thermal_.theta[1]+thermal_.theta[2]*to)/om;};
    const double* e=electric_.theta;
    const bool meter=meter_pump_only && m.electric_trusted;
    constexpr int grid=10;
    double gmin=1e9,gmax=-1e9,ua=0;
    for(int i=0;i<=grid;++i) {
        const double to=lo+(hi-lo)*i/grid,g=lift(to);
        if(!(g>0.3)) return ModelBuildError::Implausible;
        gmin=std::min(gmin,g);gmax=std::max(gmax,g);
        if(meter) {
            const double p=e[0]+e[1]*to;
            if(!(p>0.05)) return ModelBuildError::Implausible;
            ua=std::max(ua,p/g);
        }
    }
    const double g0=thermal_.theta[1]/om,g1=thermal_.theta[2]/om;
    if(!(g0>0)) return ModelBuildError::Implausible;
    // Normalised absolute scale (see header): predictions are invariant to it.
    if(!meter) ua=1.0/gmin;
    out.loss_kw_per_c=ua;
    out.thermal_mass_kwh_per_c=m.tau_h*ua;
    out.heat_kw_at_zero_c=ua*g0;
    out.heat_kw_change_per_outdoor_c=ua*g1;
    out.minimum_heat_kw=ua*std::max(1e-3,std::min(gmin,g0));
    if(!meter) {
        // Electric draw per HEAT slot is one normalised unit: costs are a
        // price-weighted runtime, never shown as kWh or currency.
        out.cop_at_zero_c=ua*g0;
        out.cop_change_per_outdoor_c=ua*g1;
        out.minimum_cop=1.0;
        out.maximum_cop=std::max(1.0,ua*gmax);
    } else {
        double sx=0,sy=0,sxx=0,sxy=0,cmin=1e9,cmax=0;
        for(int i=0;i<=grid;++i) {
            const double to=lo+(hi-lo)*i/grid;
            const double cop=ua*lift(to)/(e[0]+e[1]*to);
            sx+=to;sy+=cop;sxx+=to*to;sxy+=to*cop;
            cmin=std::min(cmin,cop);cmax=std::max(cmax,cop);
        }
        const double n=grid+1,den=n*sxx-sx*sx;
        if(!(std::fabs(den)>1e-9)) return ModelBuildError::NumericalFailure;
        out.cop_change_per_outdoor_c=(n*sxy-sx*sy)/den;
        out.cop_at_zero_c=(sy-out.cop_change_per_outdoor_c*sx)/n;
        out.minimum_cop=std::max(1.0,cmin);
        out.maximum_cop=std::max(out.minimum_cop,cmax);
        if(!(out.cop_at_zero_c>0)) out.cop_at_zero_c=out.minimum_cop;
        out.heat_standby_kw=out.off_standby_kw=m.electric_standby_kw;
        out.startup_electric_kwh=m.electric_start_kwh;
    }
    out.startup_delay_minutes=std::clamp(m.start_delay_min,0.0,14.0);
    out.stop_afterheat_kwh=std::clamp(m.afterheat_k*ua*slot_hours/om,0.0,2.0);
    // The PLANNED minimum (floor + uncertainty margin) is the same edge the
    // live thermostat/price controller heats at (target - 0.5), never below
    // the 5 C critical limit plus the margin.
    out.planning_min_c=std::max(5.0,band.lower_c-r.planning_uncertainty_c);
    out.critical_c=5.0;
    out.planning_uncertainty_c=r.planning_uncertainty_c;
    out.maximum_indoor_c=band.upper_c;
    out.variable_setpoints=variable_setpoints_;
    if(variable_setpoints_)out.thermostat_band_c=residential_thermostat_band_c;
    out.currency_limit_scale=currency_scale;
    out.switching_penalty_eur=0.02*currency_scale;
    // Owner decision 18.11.2026: >= 30 min between HEAT<->OFF changes; the
    // plan uses the same dwell as the control layer enforces.
    out.min_heat_minutes=30;
    out.min_off_minutes=30;
    if(!valid(out)) return ModelBuildError::NumericalFailure;
    energy_in_kwh=meter;
    return ModelBuildError::None;
}

// ---- persistence -------------------------------------------------------
struct IdentifierImage {
    uint32_t magic, version, identity, size;
    RlsState<BuildingIdentifier::thermal_n> thermal;
    RlsState<BuildingIdentifier::electric_n> electric;
    int64_t prev_slot_utc, window_next_utc, last_sample_utc;
    int32_t prev_heat;
    uint32_t samples, heating, off, starts, stops, skipped, elec_on;
    double outdoor_min, outdoor_max, start_outdoor_sum, stop_outdoor_sum;
    double elec_err_sq, elec_sq;
    uint32_t learning_ok, window_active, was_calibrated, window_switch;
    double window_theta[BuildingIdentifier::thermal_n];
    double window_pred, window_sq, window_max;
    uint32_t window_count;
    ValidationWindow ring[BuildingIdentifier::window_ring];
    uint32_t ring_count, ring_head, windows_aborted, revision;
    uint32_t crc;
};
struct MeterIdentifierImage {
    IdentifierImage model;
    uint32_t meter_identity;
    uint32_t crc;
};
static_assert(sizeof(MeterIdentifierImage)==sizeof(IdentifierImage)+8);
static_assert(sizeof(MeterIdentifierImage)<=BuildingIdentifier::blob_capacity,
              "identifier image must fit the NVS blob");
constexpr uint32_t identifier_magic=0x454E4D31u; // "ENM1"

bool BuildingIdentifier::save(uint32_t identity,uint8_t* out,size_t capacity,size_t& used) const {
    used=0;
    if(!out || capacity<sizeof(MeterIdentifierImage)) return false;
    MeterIdentifierImage image;
    std::memset(static_cast<void*>(&image),0,sizeof(image));
    auto& b=image.model;
    b.magic=identifier_magic;b.version=blob_version+(variable_setpoints_?1u:0u);b.identity=identity;
    b.size=sizeof(IdentifierImage);
    b.thermal=thermal_;b.electric=electric_;
    b.prev_slot_utc=prev_slot_utc_;b.window_next_utc=window_next_utc_;
    b.last_sample_utc=last_sample_utc_;b.prev_heat=prev_heat_;
    b.samples=samples_;b.heating=heating_;b.off=off_;b.starts=starts_;b.stops=stops_;
    b.skipped=skipped_;b.elec_on=elec_on_;
    b.outdoor_min=outdoor_min_;b.outdoor_max=outdoor_max_;
    b.start_outdoor_sum=start_outdoor_sum_;b.stop_outdoor_sum=stop_outdoor_sum_;
    b.elec_err_sq=elec_err_sq_;b.elec_sq=elec_sq_;
    b.learning_ok=learning_ok_;b.window_active=window_active_;
    b.was_calibrated=was_calibrated_;b.window_switch=window_switch_;
    std::memcpy(b.window_theta,window_theta_,sizeof(window_theta_));
    b.window_pred=window_pred_;b.window_sq=window_sq_;b.window_max=window_max_;
    b.window_count=window_count_;
    for(size_t i=0;i<window_ring;++i) b.ring[i]=ring_[i];
    b.ring_count=ring_count_;b.ring_head=ring_head_;
    b.windows_aborted=windows_aborted_;b.revision=revision_;
    b.crc=id_crc32(reinterpret_cast<const uint8_t*>(&b),offsetof(IdentifierImage,crc));
    image.meter_identity=electric_meter_identity_;
    image.crc=id_crc32(reinterpret_cast<const uint8_t*>(&image),
                        offsetof(MeterIdentifierImage,crc));
    std::memcpy(out,&image,sizeof(image));
    used=sizeof(image);
    return true;
}

bool BuildingIdentifier::load(uint32_t identity,const uint8_t* in,size_t size) {
    const bool has_meter=size==sizeof(MeterIdentifierImage);
    if(!in || (!has_meter && size!=sizeof(IdentifierImage))) return false;
    MeterIdentifierImage image{};
    std::memcpy(&image,in,size);
    const auto& b=image.model;
    if(has_meter && image.crc!=id_crc32(in,offsetof(MeterIdentifierImage,crc)))
        return false;
    if(b.magic!=identifier_magic || b.version!=blob_version+(variable_setpoints_?1u:0u) ||
       b.size!=sizeof(IdentifierImage) || b.identity!=identity ||
       b.crc!=id_crc32(reinterpret_cast<const uint8_t*>(&b),offsetof(IdentifierImage,crc)))
        return false;
    for(size_t i=0;i<thermal_n;++i) {
        if(!id_finite(b.thermal.theta[i]) || !id_finite(b.window_theta[i])) return false;
        for(size_t j=0;j<thermal_n;++j) if(!id_finite(b.thermal.p[i][j])) return false;
    }
    for(size_t i=0;i<electric_n;++i) {
        if(!id_finite(b.electric.theta[i])) return false;
        for(size_t j=0;j<electric_n;++j) if(!id_finite(b.electric.p[i][j])) return false;
    }
    if(b.ring_count>window_ring || b.ring_head>=window_ring || b.window_count>window_slots ||
       b.prev_heat<-1 || b.prev_heat>1 || !id_finite(b.window_pred))
        return false;
    // Every potentially failing validation is above this point. Rebuild
    // the existing object in place rather than nesting two full model
    // copies on the optimizer's internal stack. Rejected images leave the
    // previous model untouched.
    reset();
    auto& x=*this;
    x.thermal_=b.thermal;
    if(has_meter) {
        x.electric_meter_identity_=image.meter_identity;
        x.electric_=b.electric;
        x.elec_on_=b.elec_on;
        x.elec_err_sq_=b.elec_err_sq;x.elec_sq_=b.elec_sq;
    }
    x.prev_slot_utc_=b.prev_slot_utc;x.window_next_utc_=b.window_next_utc;
    x.last_sample_utc_=b.last_sample_utc;x.prev_heat_=static_cast<int8_t>(b.prev_heat);
    x.samples_=b.samples;x.heating_=b.heating;x.off_=b.off;x.starts_=b.starts;
    x.stops_=b.stops;x.skipped_=b.skipped;
    x.outdoor_min_=b.outdoor_min;x.outdoor_max_=b.outdoor_max;
    x.start_outdoor_sum_=b.start_outdoor_sum;x.stop_outdoor_sum_=b.stop_outdoor_sum;
    x.learning_ok_=b.learning_ok!=0;x.window_active_=b.window_active!=0;
    x.was_calibrated_=b.was_calibrated!=0;x.window_switch_=static_cast<uint8_t>(b.window_switch);
    std::memcpy(x.window_theta_,b.window_theta,sizeof(x.window_theta_));
    x.window_pred_=b.window_pred;x.window_sq_=b.window_sq;x.window_max_=b.window_max;
    x.window_count_=b.window_count;
    for(size_t i=0;i<window_ring;++i) x.ring_[i]=b.ring[i];
    x.ring_count_=b.ring_count;x.ring_head_=b.ring_head;
    x.windows_aborted_=b.windows_aborted;x.revision_=b.revision;
    x.refresh_state();
    return true;
}
} // namespace enevina::cottage
