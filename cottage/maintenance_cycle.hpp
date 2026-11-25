#pragma once
// Compare complete, locally observed HEAT-request -> OFF -> HEAT-request
// cycles, including all intervening electrical consumption and cooldown.
// No guessed COP, no fixed run time, and no extra I/O or allocation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "cottage/baseload_learning.hpp"

namespace enevina::cottage {
inline constexpr unsigned maintenance_cycle_candidates=3;
inline constexpr double maintenance_model_ceiling_c=15.8;
// Preserve the start threshold. Explore storage room, not arbitrary
// compressor dwell. A native 16 C thermostat remains in control of HEAT.
inline double maintenance_cycle_ceiling(double target,double owner_work_upper,unsigned candidate) {
    const double upper=std::max(target,std::min(maintenance_model_ceiling_c,
        owner_work_upper>0?owner_work_upper:maintenance_model_ceiling_c));
    const double fraction=std::min(candidate,maintenance_cycle_candidates-1)/2.0;
    return target+(upper-target)*fraction;
}

struct MaintenanceCycleSample {
    int64_t utc=0;
    double room_c=0,outdoor_c=0,total_wh=0,base_w=0,price=0;
    uint32_t meter_id=0,base_revision=0,endpoint=0,link=0,request_generation=0;
    uint32_t sent=0,confirmed=0,failed=0;
    int64_t ack_utc=0;
    bool heat=false,valid=false,price_valid=false;
};
struct MaintenanceCycleObservation {
    bool valid=false;
    double energy_kwh=0,hours=0,outdoor_c=0,mean_room_c=0,peak_c=0,cost=0;
    bool cost_valid=false;
    double rate_kw() const {return hours>0?energy_kwh/hours:0;}
};
struct MaintenanceCycleReport {
    bool enabled=false;
    uint32_t site_generation=0,config_generation=0,meter_identity=0;
    double target_c=0;
    unsigned candidate=2;
    uint32_t completed=0,rejected=0;
    std::array<unsigned,maintenance_cycle_candidates> compared{};
    std::array<double,maintenance_cycle_candidates> rate_kw{};
    MaintenanceCycleObservation last{};
};

// Keep three recent complete cycles per candidate. Compare like outdoor
// conditions (within 1.5 C); require two observations of each candidate
// before selecting a winner. Explore least-observed candidates first,
// and periodically revisit one alternative. Small differences (<5%) do
// not justify keeping the hotter candidate. Counts are RAM-only; the
// independently identified MPC thermal/electric model persists as before.
class MaintenanceCycleLearner {
public:
    MaintenanceCycleReport observe(bool enabled,uint32_t identity,uint32_t site_generation,
            uint32_t config_generation,double target,const MaintenanceCycleSample& s) {
        if(!enabled || identity!=identity_ || site_generation!=site_ || config_generation!=config_ ||
           target!=target_) {
            *this=MaintenanceCycleLearner{};
            enabled_=enabled;identity_=identity;site_=site_generation;config_=config_generation;target_=target;
        }
        if(!enabled || !valid_sample(s,identity)) {
            reject_open();
            have_previous_=false;
            return report(s.outdoor_c);
        }
        // A new qualified background-load regime changes heat balance as
        // well as the electrical reference. Reject the partial cycle and
        // discard completed comparisons even after an observation gap.
        if(regime_w_>=0 &&
           std::fabs(s.base_w-regime_w_)>std::max(25.0,0.15*regime_w_)) {
            reject_open();
            records_={};next_={};last_={};completed_=0;
            candidate_=2;
            have_previous_=false;
            regime_w_=-1;
        }
        if(regime_w_<0)regime_w_=s.base_w;
        if(!have_previous_) {previous_=s;have_previous_=true;return report(s.outdoor_c);}
        const auto p=previous_;
        const int64_t dt=s.utc-p.utc;
        if(dt==0 && s.request_generation==p.request_generation && s.total_wh==p.total_wh &&
           s.sent==p.sent && s.confirmed==p.confirmed && s.failed==p.failed)
            return report(s.outdoor_c);
        previous_=s;
        const bool transition=s.heat!=p.heat;
        const bool same=s.endpoint==p.endpoint && s.link==p.link && s.meter_id==p.meter_id &&
            s.base_revision==p.base_revision && std::fabs(s.base_w-p.base_w)<0.01 &&
            s.failed==p.failed && dt>0 && dt<=150 && s.total_wh>=p.total_wh;
        const bool command=transition &&
            s.request_generation==p.request_generation+1 && s.sent==p.sent+1 &&
            s.confirmed==p.confirmed+1 && s.ack_utc>=p.utc-15 && s.ack_utc<=s.utc+15;
        const bool quiet=!transition && s.request_generation==p.request_generation &&
            s.sent==p.sent && s.confirmed==p.confirmed;
        if(!same || !(command||quiet)) {
            reject_open();return report(s.outdoor_c);
        }
        // The first sample after HEAT is bracketed by the prior OFF sample.
        // Use that prior sample as the shared boundary between full cycles,
        // including the next start's entire energy increment in the new one.
        if(transition && s.heat) {
            if(open_)finish(p);
            start(p);
            candidate_=choose(s.outdoor_c);
            active_candidate_=candidate_;
        }
        if(open_) {
            if(transition && !s.heat) {
                if(stopped_){reject_open();return report(s.outdoor_c);}
                stopped_=true;stop_utc_=s.ack_utc;
            }
            const double hours=dt/3600.0;
            const double net=(s.total_wh-p.total_wh-s.base_w*hours)/1000.0;
            // Noise around the baseline may be slightly negative. A material
            // unexplained load step or OFF power is not valid cycle evidence.
            const double tolerance=std::max(15.0,0.10*s.base_w)*hours/1000.0;
            if(!std::isfinite(net) || net < -tolerance || net>20*hours ||
               (!p.heat && !s.heat && stopped_ && p.utc-stop_utc_>=240 && std::fabs(net)>tolerance)) {
                reject_open();return report(s.outdoor_c);
            }
            energy_+=net;
            outdoor_hours_+=(p.outdoor_c+s.outdoor_c)*0.5*hours;
            room_hours_+=(p.room_c+s.room_c)*0.5*hours;
            peak_=std::max(peak_,s.room_c);
            price_ok_=price_ok_ && p.price_valid && s.price_valid;
            if(price_ok_)cost_+=net*(p.price+s.price)*0.5;
            if(s.utc-start_utc_>24*3600)reject_open();
        }
        return report(s.outdoor_c);
    }
    MaintenanceCycleReport report(double outdoor) const {
        MaintenanceCycleReport r{};
        r.enabled=enabled_;r.candidate=candidate_;r.completed=completed_;r.rejected=rejected_;r.last=last_;
        r.site_generation=site_;r.config_generation=config_;r.meter_identity=identity_;r.target_c=target_;
        for(unsigned i=0;i<maintenance_cycle_candidates;++i) {
            double e=0,t=0;
            for(const auto& x:records_[i])if(x.valid && std::isfinite(outdoor) &&
                    std::fabs(x.outdoor_c-outdoor)<=1.5) {
                ++r.compared[i];e+=x.energy_kwh;t+=x.hours;
            }
            r.rate_kw[i]=t>0?e/t:0;
        }
        return r;
    }
private:
    static bool valid_sample(const MaintenanceCycleSample& s,uint32_t id) {
        return s.valid && id && s.meter_id==id && s.utc>0 &&
            std::isfinite(s.room_c) && s.room_c>=-10 && s.room_c<=40 &&
            std::isfinite(s.outdoor_c) && s.outdoor_c>=-55 && s.outdoor_c<=45 &&
            std::isfinite(s.total_wh) && s.total_wh>=0 &&
            std::isfinite(s.base_w) && s.base_w>=0 && s.base_w<=20000 &&
            (!s.price_valid || std::isfinite(s.price));
    }
    void reject_open(){if(open_)++rejected_;open_=false;}
    void start(const MaintenanceCycleSample& p) {
        open_=true;stopped_=false;start_utc_=p.utc;start_room_=peak_=p.room_c;
        energy_=outdoor_hours_=room_hours_=cost_=0;price_ok_=p.price_valid;
    }
    void finish(const MaintenanceCycleSample& end) {
        const double hours=(end.utc-start_utc_)/3600.0;
        // Same starting/ending room temperature => no benefit from merely
        // depleting the building. Both boundaries must be near the target.
        if(!stopped_ || hours<0.25 || std::fabs(end.room_c-start_room_)>0.5 ||
           std::fabs(start_room_-target_)>1.25 || std::fabs(end.room_c-target_)>1.25 ||
           !std::isfinite(energy_) || energy_<=0) {reject_open();return;}
        MaintenanceCycleObservation x{true,energy_,hours,outdoor_hours_/hours,
            room_hours_/hours,peak_,cost_,price_ok_};
        records_[active_candidate_][next_[active_candidate_]++%3]=x;
        last_=x;++completed_;open_=false;
    }
    unsigned choose(double outdoor) const {
        const auto r=report(outdoor);
        // Initial sample uses the wide candidate; then compare short and
        // intermediate cycles, rather than assuming wider is always better.
        for(unsigned i: {2u,0u,1u})if(r.compared[i]==0)return i;
        for(unsigned i: {2u,0u,1u})if(r.compared[i]<2)return i;
        unsigned best=0;
        for(unsigned i=1;i<maintenance_cycle_candidates;++i)
            if(r.rate_kw[i]<r.rate_kw[best]*0.95)best=i;
        if(completed_%8==0)return (best+1+((completed_/8)%2))%maintenance_cycle_candidates;
        return best;
    }
    bool enabled_=false,open_=false,stopped_=false,have_previous_=false,price_ok_=false;
    uint32_t identity_=0,site_=0,config_=0,completed_=0,rejected_=0;
    unsigned candidate_=2,active_candidate_=2;
    double target_=0,start_room_=0,peak_=0,energy_=0,outdoor_hours_=0,room_hours_=0,cost_=0;
    double regime_w_=-1;
    int64_t start_utc_=0,stop_utc_=0;
    MaintenanceCycleSample previous_{};
    MaintenanceCycleObservation last_{};
    std::array<std::array<MaintenanceCycleObservation,3>,maintenance_cycle_candidates> records_{};
    std::array<unsigned,maintenance_cycle_candidates> next_{};
};
} // namespace enevina::cottage
