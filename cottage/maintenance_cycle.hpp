#pragma once
// Compare complete, locally observed HEAT-request -> OFF -> HEAT-request
// cycles, including all intervening electrical consumption and cooldown.
// No guessed COP, no fixed run time, and no extra I/O or allocation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <new>
#include "cottage/history_persist.hpp"
#include "cottage/baseload_learning.hpp"
#include "cottage/maintenance_phase.hpp"

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
    bool heat=false,valid=false,price_valid=false,price_priority=false,tariff_adjusted=false;
    CycleStrategy strategy=CycleStrategy::Existing;
    uint32_t trial_revision=0;
    double trial_lower=0,trial_upper=0;
    HeatPhase phase=HeatPhase::Unknown;
    bool phase_stop_command=false;
    bool measurement_gap_only=false; // no conflicting request, meter or baseline evidence
};
struct MaintenanceCycleObservation {
    bool valid=false;
    double energy_kwh=0,hours=0,outdoor_c=0,mean_room_c=0,peak_c=0,cost=0;
    bool cost_valid=false;
    double start_room_c=0,end_room_c=0;
    int64_t end_utc=0;
    double rate_kw() const {return hours>0?energy_kwh/hours:0;}
};
struct TrialCycleObservation {
    MaintenanceCycleObservation cycle{};
    double total_kwh=0,total_cost=0,run_hours=0,off_hours=0,min_room_c=0;
    double startup_kwh=0,reported_defrost_kwh=0,baseload_kwh=0,loss_degree_hours=0;
    double lower=0,upper=0,mean_tariff=0;
    bool startup_seen=false,defrost_seen=false,phase_end_seen=false;
};
struct MaintenanceCycleReport {
    bool enabled=false,restored=false,checkpoint_dirty=false,checkpoint_available=false;
    uint32_t site_generation=0,config_generation=0,meter_identity=0;
    double target_c=0;
    unsigned candidate=0;
    uint32_t completed=0,rejected=0,tariff_completed=0,checkpoint_writes=0,checkpoint_failures=0,restore_failures=0;
    std::array<unsigned,maintenance_cycle_candidates> compared{};
    std::array<double,maintenance_cycle_candidates> rate_kw{};
    MaintenanceCycleObservation last{},last_tariff{};
    std::array<TrialCycleObservation,4> trials{};
    std::array<unsigned,4> trial_counts{};
    std::array<unsigned,4> trial_matched{};
    std::array<double,4> trial_cost_per_hour{};
};

inline CycleStrategy select_observed_cycle_strategy(const MaintenanceCycleReport& evidence,
        double lower,double upper,bool phase_supported) {
    if(evidence.trial_matched[0]<2 || evidence.trials[0].lower!=lower ||
       evidence.trials[0].upper!=upper)return CycleStrategy::Forecast;
    size_t best=0;
    for(size_t i=1;i<evidence.trial_matched.size();++i)
        if(evidence.trial_matched[i]>=2 && (i==3 || phase_supported) &&
           evidence.trial_cost_per_hour[i]<evidence.trial_cost_per_hour[best])best=i;
    return static_cast<CycleStrategy>(best+1);
}

// Keep three recent complete cycles per candidate. Compare like outdoor
// conditions (within 1.5 C); require two observations of each candidate
// before selecting a winner. Explore least-observed candidates first,
// and periodically revisit one alternative. Small differences (<5%) do
// not justify keeping the hotter candidate. Only completed observations
// are checkpointed; live requests, counters and partial cycles are never
// restored across a reboot or OTA.
// The key is stable across reboot but scoped to model epoch, meter, owner
// settings and tariff. Volatile link/configuration generations are not saved.
struct MaintenanceCycleKey {
    uint32_t identity=0,meter=0,tariff=0;
    double target=0,lower=0,upper=0;
    bool valid() const {
        return identity && meter && tariff && std::isfinite(target) &&
            std::isfinite(lower) && std::isfinite(upper) && target>=5 &&
            target<=26 && lower>=0 && upper>=0 && lower<=26 && upper<=26 &&
            (lower==0 || lower<=target-0.5) && (upper==0 || upper>=target+0.5);
    }
    bool operator==(const MaintenanceCycleKey& b) const {
        return identity==b.identity && meter==b.meter && tariff==b.tariff &&
            target==b.target && lower==b.lower && upper==b.upper;
    }
};
struct MaintenanceCycleDiskRecord {
    int64_t end_utc=0;
    double energy=0,hours=0,outdoor=0,mean=0,peak=0,cost=0,start=0,end=0;
    uint8_t flags=0,reserved[7]{};
};
struct MaintenanceCycleImage {
    uint32_t magic=0x31434D45u,version=1; // EMC1, little endian
    uint32_t identity=0,meter=0,tariff=0,completed=0,tariff_completed=0,reserved=0;
    double target=0,lower=0,upper=0,regime_w=0;
    MaintenanceCycleDiskRecord records[3][3]{},last{},last_tariff{};
    uint8_t next[3]{},pad[5]{};
    uint16_t crc=0;
    uint8_t tail[6]{};
};
static_assert(sizeof(MaintenanceCycleDiskRecord)==80,"cycle record layout");
static_assert(offsetof(MaintenanceCycleImage,crc)==952 && sizeof(MaintenanceCycleImage)==960,
              "cycle checkpoint layout");
class MaintenanceCycleLearner {
public:
    void reset(){this->~MaintenanceCycleLearner();new(this) MaintenanceCycleLearner{};}
    static constexpr size_t checkpoint_size=sizeof(MaintenanceCycleImage);
    static constexpr int64_t record_max_age_s=30*86400;
    bool checkpoint_dirty() const {return dirty_;}
    bool has_trial_results(int64_t now) const {
        return std::any_of(trials_.begin(),trials_.end(),[now](const auto& item) {
            return recent(item.cycle,now);
        });
    }
    bool can_restore() const {return !open_ && !dirty_;}
    void checkpoint_saved(){dirty_=false;}
    // Writes a versioned image to caller-owned memory. Never store a partial
    // run, request generations, or a possibly unconfirmed pump command.
    bool save(const MaintenanceCycleKey& key,MaintenanceCycleImage& image) const {
        if(!enabled_ || !key.valid() || key.meter!=identity_ || key.target!=target_ ||
           !std::isfinite(regime_w_) || regime_w_<0)return false;
        new(&image) MaintenanceCycleImage{};
        image.identity=key.identity;image.meter=key.meter;image.tariff=key.tariff;
        image.target=key.target;image.lower=key.lower;image.upper=key.upper;
        image.regime_w=regime_w_;image.completed=completed_;image.tariff_completed=tariff_completed_;
        for(unsigned i=0;i<3;++i) {
            image.next[i]=static_cast<uint8_t>(next_[i]%3);
            for(unsigned j=0;j<3;++j)image.records[i][j]=encode(records_[i][j]);
        }
        image.last=encode(last_);image.last_tariff=encode(last_tariff_);
        image.crc=history_crc16(reinterpret_cast<const uint8_t*>(&image),offsetof(MaintenanceCycleImage,crc));
        return true;
    }
    // Restore only on a qualified fresh meter/room/pump observation and
    // the same measured heat-gain regime. Failure leaves this learner alone.
    bool restore(const MaintenanceCycleKey& key,uint32_t site_generation,uint32_t config_generation,
                 const MaintenanceCycleSample& sample,const MaintenanceCycleImage& image) {
        if(!key.valid() || !valid_sample(sample,key.meter) ||
           image.magic!=0x31434D45u || image.version!=1 || image.reserved!=0 ||
           image.crc!=history_crc16(reinterpret_cast<const uint8_t*>(&image),offsetof(MaintenanceCycleImage,crc)) ||
           !(key==MaintenanceCycleKey{image.identity,image.meter,image.tariff,image.target,image.lower,image.upper}) ||
           !std::isfinite(image.regime_w) || image.regime_w<0 || image.regime_w>20000 ||
           std::fabs(sample.base_w-image.regime_w)>std::max(25.0,0.15*image.regime_w))return false;
        for(unsigned i=0;i<3;++i) {
            if(image.next[i]>=3)return false;
            for(unsigned j=0;j<3;++j)if(!valid_record(image.records[i][j],sample.utc))return false;
        }
        if(!valid_record(image.last,sample.utc) || !valid_record(image.last_tariff,sample.utc))return false;
        reset();
        enabled_=true;identity_=key.meter;site_=site_generation;config_=config_generation;target_=key.target;
        regime_w_=image.regime_w;completed_=image.completed;tariff_completed_=image.tariff_completed;
        last_seen_utc_=sample.utc;
        for(unsigned i=0;i<3;++i) {
            next_[i]=image.next[i];
            for(unsigned j=0;j<3;++j)records_[i][j]=decode(image.records[i][j],sample.utc);
        }
        last_=decode(image.last,sample.utc);last_tariff_=decode(image.last_tariff,sample.utc);
        // Never resume an unfinished experiment at a high ceiling. Rechoose
        // only from comparable, qualified completed cycles, or candidate 0.
        candidate_=choose(sample.outdoor_c,true);restored_=true;
        return true;
    }
    MaintenanceCycleReport observe(bool enabled,uint32_t identity,uint32_t site_generation,
            uint32_t config_generation,double target,const MaintenanceCycleSample& s) {
        if(!enabled || identity!=identity_ || site_generation!=site_ || config_generation!=config_ ||
           target!=target_) {
            reset();
            enabled_=enabled;identity_=identity;site_=site_generation;config_=config_generation;target_=target;
        }
        if(!enabled || !valid_sample(s,identity)) {
            // A missed room/meter sample is not a foreign command. Keep
            // the last qualified counter boundary only for a bounded gap
            // and only while every identity/command/baseline remains equal.
            // The next accepted interval still has to pass all normal checks.
            const auto& p=previous_;
            const bool bridge=enabled && have_previous_ && s.measurement_gap_only &&
                s.utc>=p.utc && s.utc-p.utc<=150 && s.meter_id==identity &&
                s.trial_revision==p.trial_revision && s.strategy==p.strategy &&
                std::isfinite(s.total_wh) && s.total_wh>=p.total_wh &&
                s.endpoint==p.endpoint && s.link==p.link && s.base_revision==p.base_revision &&
                std::fabs(s.base_w-p.base_w)<0.01 && s.heat==p.heat &&
                s.request_generation==p.request_generation && s.sent==p.sent &&
                s.confirmed==p.confirmed && s.failed==p.failed;
            if(bridge) {
                if(open_) {
                    tariff_adjusted_=tariff_adjusted_ || s.tariff_adjusted;
                    price_ok_=price_ok_ && s.price_valid;
                }
                return report(s.outdoor_c);
            }
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
            records_={};next_={};trials_={};previous_trials_={};trial_counts_={};last_={};last_tariff_={};completed_=0;tariff_completed_=0;dirty_=true;restored_=false;
            candidate_=0;
            have_previous_=false;
            regime_w_=-1;
        }
        if(regime_w_<0)regime_w_=s.base_w;
        last_seen_utc_=s.utc;
        if(!have_previous_) {previous_=s;have_previous_=true;return report(s.outdoor_c);}
        const auto p=previous_;
        const int64_t dt=s.utc-p.utc;
        if(dt==0 && s.request_generation==p.request_generation && s.total_wh==p.total_wh &&
           s.sent==p.sent && s.confirmed==p.confirmed && s.failed==p.failed)
            return report(s.outdoor_c);
        previous_=s;
        const bool transition=s.heat!=p.heat;
        const bool same=s.trial_revision==p.trial_revision && s.strategy==p.strategy &&
            s.endpoint==p.endpoint && s.link==p.link && s.meter_id==p.meter_id &&
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
            active_trial_=s.strategy;
            trial_stats_={};
            trial_tariff_hours_=0;
            trial_stats_.min_room_c=p.room_c;
            trial_stats_.lower=s.trial_lower;trial_stats_.upper=s.trial_upper;
            candidate_=choose(s.outdoor_c,s.price_priority);
            active_candidate_=candidate_;
        }
        if(open_) {
            tariff_adjusted_=tariff_adjusted_ || p.tariff_adjusted || s.tariff_adjusted;
            if(transition && !s.heat) {
                if(stopped_){reject_open();return report(s.outdoor_c);}
                stopped_=true;stop_utc_=s.ack_utc;
                trial_stats_.phase_end_seen=s.phase_stop_command;
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
            if(active_trial_!=CycleStrategy::Existing) {
                const double total=(s.total_wh-p.total_wh)/1000;
                const double heat_hours=transition?
                    std::clamp((s.heat?s.utc-s.ack_utc:s.ack_utc-p.utc)/3600.0,0.0,hours):
                    (s.heat?hours:0);
                trial_stats_.total_kwh+=total;
                trial_stats_.run_hours+=heat_hours;
                trial_stats_.off_hours+=hours-heat_hours;
                trial_stats_.min_room_c=std::min(trial_stats_.min_room_c,s.room_c);
                trial_stats_.baseload_kwh+=s.base_w*hours/1000;
                trial_stats_.loss_degree_hours+=std::max(0.0,
                    (p.room_c+s.room_c-p.outdoor_c-s.outdoor_c)*0.5)*hours;
                if(s.heat && s.phase==HeatPhase::Starting) {
                    trial_stats_.startup_seen=true;
                    trial_stats_.startup_kwh+=std::max(0.0,net);
                }
                if(s.heat && (s.phase==HeatPhase::Defrost || s.phase==HeatPhase::Recovery)) {
                    trial_stats_.defrost_seen=true;
                    trial_stats_.reported_defrost_kwh+=std::max(0.0,net);
                }
                if(p.price_valid && s.price_valid) {
                    const double rate=(p.price+s.price)*0.5;
                    trial_stats_.total_cost+=total*rate;
                    trial_tariff_hours_+=hours*rate;
                }
            }
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
        r.enabled=enabled_;r.restored=restored_;r.checkpoint_dirty=dirty_;r.candidate=candidate_;r.completed=completed_;r.rejected=rejected_;
        if(recent(last_,last_seen_utc_))r.last=last_;
        r.tariff_completed=tariff_completed_;
        for(size_t i=0;i<trials_.size();++i)if(recent(trials_[i].cycle,last_seen_utc_)) {
            r.trials[i]=trials_[i];r.trial_counts[i]=trial_counts_[i];
            const auto& baseline=trials_[0];
            if(!recent(baseline.cycle,last_seen_utc_) ||
               !std::isfinite(outdoor) || std::fabs(outdoor-baseline.cycle.outdoor_c)>1.5)
                continue;
            double cost=0,hours=0;
            for(const auto* record:{&trials_[i],&previous_trials_[i]}) {
                if(!recent(record->cycle,last_seen_utc_) || !comparable(*record,baseline))continue;
                if((i==1 || i==2) && !record->phase_end_seen)continue;
                ++r.trial_matched[i];cost+=record->total_cost;hours+=record->cycle.hours;
            }
            if(hours>0)r.trial_cost_per_hour[i]=cost/hours;
        }
        if(recent(last_tariff_,last_seen_utc_))r.last_tariff=last_tariff_;
        r.site_generation=site_;r.config_generation=config_;r.meter_identity=identity_;r.target_c=target_;
        for(unsigned i=0;i<maintenance_cycle_candidates;++i) {
            double e=0,t=0;
            for(const auto& x:records_[i])if(recent(x,last_seen_utc_) && std::isfinite(outdoor) &&
                    std::fabs(x.outdoor_c-outdoor)<=1.5) {
                ++r.compared[i];e+=x.energy_kwh;t+=x.hours;
            }
            r.rate_kw[i]=t>0?e/t:0;
        }
        return r;
    }
private:
    // These are matched observations, not a counterfactual tariff replay.
    // Require the same owner envelope and thermal endpoints, comparable
    // weather and a comparable mean tariff before ranking measured costs.
    static bool comparable(const TrialCycleObservation& a,const TrialCycleObservation& b) {
        return a.cycle.cost_valid && b.cycle.cost_valid && a.lower==b.lower && a.upper==b.upper &&
            std::abs(a.cycle.start_room_c-b.cycle.start_room_c)<=0.5 &&
            std::abs(a.cycle.end_room_c-b.cycle.end_room_c)<=0.5 &&
            std::abs(a.cycle.outdoor_c-b.cycle.outdoor_c)<=1.5 &&
            std::abs(a.mean_tariff-b.mean_tariff)<=0.1*std::max(std::abs(b.mean_tariff),0.01);
    }
    static bool recent(const MaintenanceCycleObservation& x,int64_t now) {
        return x.valid && x.end_utc>0 && now>=x.end_utc && now-x.end_utc<=record_max_age_s;
    }
    static MaintenanceCycleDiskRecord encode(const MaintenanceCycleObservation& x) {
        if(!x.valid)return {};
        MaintenanceCycleDiskRecord r{};
        r.end_utc=x.end_utc;r.energy=x.energy_kwh;r.hours=x.hours;r.outdoor=x.outdoor_c;
        r.mean=x.mean_room_c;r.peak=x.peak_c;r.cost=x.cost_valid?x.cost:0;
        r.start=x.start_room_c;r.end=x.end_room_c;
        r.flags=static_cast<uint8_t>(1|(x.cost_valid?2:0));return r;
    }
    static bool valid_record(const MaintenanceCycleDiskRecord& r,int64_t now) {
        if(r.flags==0)return true;
        if(r.flags!=1 && r.flags!=3)return false;
        return r.end_utc>0 && r.end_utc<=now &&
            std::isfinite(r.hours) && r.hours>=0.25 && r.hours<=24 &&
            std::isfinite(r.energy) && r.energy>0 && r.energy<=20*r.hours &&
            std::isfinite(r.outdoor) && r.outdoor>=-55 && r.outdoor<=45 &&
            std::isfinite(r.mean) && r.mean>=-10 && r.mean<=40 &&
            std::isfinite(r.peak) && r.peak>=-10 && r.peak<=40 &&
            std::isfinite(r.start) && r.start>=-10 && r.start<=40 &&
            std::isfinite(r.end) && r.end>=-10 && r.end<=40 && std::isfinite(r.cost);
    }
    static MaintenanceCycleObservation decode(const MaintenanceCycleDiskRecord& r,int64_t now) {
        if(!r.flags || now-r.end_utc>record_max_age_s)return {};
        return {true,r.energy,r.hours,r.outdoor,r.mean,r.peak,r.cost,
            (r.flags&2)!=0,r.start,r.end,r.end_utc};
    }
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
        energy_=outdoor_hours_=room_hours_=cost_=0;price_ok_=p.price_valid;tariff_adjusted_=p.tariff_adjusted;
    }
    void finish(const MaintenanceCycleSample& end) {
        const double hours=(end.utc-start_utc_)/3600.0;
        // Same starting/ending room temperature => no benefit from merely
        // depleting the building. Both boundaries must be near the target.
        if(!stopped_ || hours<0.25 || !std::isfinite(energy_) || energy_<=0) {
            reject_open();return;
        }
        MaintenanceCycleObservation x{true,energy_,hours,outdoor_hours_/hours,
            room_hours_/hours,peak_,cost_,price_ok_,start_room_,end.room_c,end.utc};
        if(active_trial_!=CycleStrategy::Existing) {
            if(std::fabs(end.room_c-start_room_)>0.5 ||
               trial_stats_.min_room_c<trial_stats_.lower || peak_>trial_stats_.upper) {
                reject_open();return;
            }
            const size_t i=static_cast<size_t>(active_trial_)-1;
            trial_stats_.cycle=x;
            trial_stats_.mean_tariff=price_ok_?trial_tariff_hours_/hours:0;
            previous_trials_[i]=trials_[i];
            trials_[i]=trial_stats_;
            ++trial_counts_[i];
            open_=false;
            return;
        }
        // Preserve observed shifted cycles, even with unequal thermal
        // endpoints. They are evidence, not a comparable saving experiment.
        if(tariff_adjusted_) {
            last_tariff_=x;++tariff_completed_;dirty_=true;open_=false;return;
        }
        if(std::fabs(end.room_c-start_room_)>0.5 ||
           std::fabs(start_room_-target_)>1.25 || std::fabs(end.room_c-target_)>1.25) {
            reject_open();return;
        }
        records_[active_candidate_][next_[active_candidate_]++%3]=x;
        last_=x;++completed_;dirty_=true;open_=false;
    }
    unsigned choose(double outdoor,bool price_priority) const {
        const auto r=report(outdoor);
        if(price_priority) {
            // During costly periods do not spend electricity to explore
            // an unmeasured storage cycle. Exploit qualified local evidence,
            // or retain the shortest target cycle until there is evidence.
            unsigned best=0;
            if(r.compared[0]<2)return 0;
            for(unsigned i: {1u,2u})
                if(r.compared[i]>=2 && r.rate_kw[i]<r.rate_kw[best]*0.95)best=i;
            return best;
        }
        // Start at the user's ordinary target. Explore wider storage
        // only after observing the less wasteful candidate.
        for(unsigned i: {0u,1u,2u})if(r.compared[i]==0)return i;
        for(unsigned i: {0u,1u,2u})if(r.compared[i]<2)return i;
        unsigned best=0;
        for(unsigned i=1;i<maintenance_cycle_candidates;++i)
            if(r.rate_kw[i]<r.rate_kw[best]*0.95)best=i;
        if(completed_%8==0)return (best+1+((completed_/8)%2))%maintenance_cycle_candidates;
        return best;
    }
    bool enabled_=false,open_=false,stopped_=false,have_previous_=false,price_ok_=false,tariff_adjusted_=false,dirty_=false,restored_=false;
    uint32_t identity_=0,site_=0,config_=0,completed_=0,rejected_=0,tariff_completed_=0;
    unsigned candidate_=0,active_candidate_=0;
    double target_=0,start_room_=0,peak_=0,energy_=0,outdoor_hours_=0,room_hours_=0,cost_=0;
    double regime_w_=-1;
    int64_t start_utc_=0,stop_utc_=0,last_seen_utc_=0;
    MaintenanceCycleSample previous_{};
    MaintenanceCycleObservation last_{},last_tariff_{};
    CycleStrategy active_trial_=CycleStrategy::Existing;
    TrialCycleObservation trial_stats_{};
    double trial_tariff_hours_=0;
    std::array<TrialCycleObservation,4> trials_{},previous_trials_{};
    std::array<unsigned,4> trial_counts_{};
    std::array<std::array<MaintenanceCycleObservation,3>,maintenance_cycle_candidates> records_{};
    std::array<unsigned,maintenance_cycle_candidates> next_{};
};
} // namespace enevina::cottage
