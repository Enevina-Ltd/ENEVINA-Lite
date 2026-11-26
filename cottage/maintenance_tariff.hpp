#pragma once
// Price-aware fallback for below-device-minimum room maintenance.
// Uses the same learned normalised thermal model as MPC, but does NOT
// represent an unvalidated electrical model or claim euros saved.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include "cottage/forecast_bridge.hpp"
#include "cottage/building_identification.hpp"
#include "cottage/maintenance_cycle.hpp"
#include "cottage/price_bootstrap.hpp"

namespace enevina::cottage {
enum class MaintenanceTariffAction : uint8_t { None, Preheat, Coast, FinishStorage };
inline const char* maintenance_tariff_name(MaintenanceTariffAction a) {
    switch(a) {
    case MaintenanceTariffAction::Preheat:return "PREHEAT_BEFORE_PRICE_RISE";
    case MaintenanceTariffAction::Coast:return "COAST_TO_CHEAPER_SLOT";
    case MaintenanceTariffAction::FinishStorage:return "FINISH_PREHEAT_CYCLE";
    default:return "NORMAL_ENERGY_CYCLE";
    }
}
struct MaintenanceTariffAdvice {
    MaintenanceTariffAction action=MaintenanceTariffAction::None;
    bool valid=false,model_based=false,avoid_exploration=false;
    int64_t computed_utc=0,alternative_utc=0,deadline_utc=0,price_end_utc=0,next_start_utc=0;
    uint32_t site_generation=0,config_generation=0,price_revision=0,region_generation=0;
    double current_price=0,alternative_price=0,ceiling_c=0,required_room_c=0;
};
// One shared optimizer-owned PSRAM workspace; no HTTP-stack allocation.
inline constexpr unsigned tariff_step_minutes=5,tariff_max_steps=288;
struct MaintenanceTariffScratch {
    float price[tariff_max_steps]{};
    float outdoor[tariff_max_steps]{};
    float value[tariff_max_steps+1]{};
    uint16_t start[tariff_max_steps+1]{};
};
struct MaintenanceTariffInput {
    int64_t now=0;
    double room_c=0,target_c=0,lower_c=0,storage_ceiling_c=0,cycle_ceiling_c=0;
    double outside_c=0,price_scale=1;
    bool heating=false;
    unsigned minutes_in_mode=0;
    MaintenanceTariffScratch* scratch=nullptr;
    const enevina::OutdoorPoint* weather=nullptr;
    size_t weather_count=0;
    const Spot15* prices=nullptr; // already TOTAL price, not raw spot
    size_t count=0;
    ModelEstimate model{};
    uint32_t samples=0,off_samples=0;
};
inline bool maintenance_tariff_price(double p,double scale) {
    return std::isfinite(p) && p>=-scale && p<=10*scale;
}
inline double maintenance_tariff_mean(const Spot15* p,size_t n,int64_t begin,int64_t end) {
    if(!p || !n || end<=begin || begin<p[0].utc || end>p[n-1].utc+900)return NAN;
    double sum=0;
    for(size_t i=0;i<n;++i) {
        const auto a=std::max(begin,p[i].utc),b=std::min(end,p[i].utc+900);
        if(b>a)sum+=p[i].eur_per_kwh*double(b-a);
        if(p[i].utc+900>=end)break;
    }
    return sum/double(end-begin);
}
inline double maintenance_loss_adjusted_price(double price,double delay_h,double tau_h) {
    // Heat bought earlier decays before it is needed. Negative marginal
    // prices retain their sign; do not pretend heat loss creates revenue.
    return price+std::max(0.0,price)*std::expm1(std::max(0.0,delay_h)/tau_h);
}
inline double maintenance_coast_hours(double room,double floor,double equilibrium,double tau) {
    if(room<=floor)return 0;
    if(equilibrium>=floor)return 24;
    return std::clamp(tau*std::log((room-equilibrium)/(floor-equilibrium)),0.0,24.0);
}
// Event-based dynamic programming: only the start of a complete charge
// is shifted. All simulated charges finish at the same selected ceiling.
// This avoids a full-slot MPC choosing repeated short HEAT requests simply
// because its thermal model has not yet identified electrical startup cost.
// Each OFF node is the end of one charge; a candidate transition waits at
// most until the predicted floor and integrates a full subsequent charge
// against the actual marginal tariff. Terminal heat is restored to target.
struct TariffCharge {
    unsigned end=0;
    float cost=0,room=0;
    bool valid=false;
};
struct TariffSearch {
    float best=INFINITY,later=INFINITY;
    unsigned start=0;
};
inline bool tariff_outdoor_at(const MaintenanceTariffInput& in,int64_t utc,double& value) {
    if(!in.weather || in.weather_count<2)return false;
    for(size_t i=0;i+1<in.weather_count;++i) {
        const auto& a=in.weather[i];const auto& b=in.weather[i+1];
        if(a.timestamp_utc<=utc && b.timestamp_utc>utc) {
            if(b.timestamp_utc-a.timestamp_utc!=3600 ||
               !std::isfinite(a.temperature_c)||!std::isfinite(b.temperature_c))return false;
            value=a.temperature_c+(b.temperature_c-a.temperature_c)*
                double(utc-a.timestamp_utc)/3600.0;
            return value>=-55&&value<=45;
        }
    }
    return false;
}
struct TariffCycleSolver {
    const MaintenanceTariffInput& in;
    MaintenanceTariffScratch& w;
    unsigned n=0;
    float tau=0,decay=0,gain=0,lift=0,lift_slope=0,lift_ref=0,delay=0,afterheat=0,ceiling=0,floor=0;
    float cool(float room,unsigned t)const {
        const float eq=w.outdoor[t]+gain;
        return eq+(room-eq)*decay;
    }
    float heating_lift(unsigned t)const {
        return lift+lift_slope*(w.outdoor[t]-lift_ref);
    }
    float off_node_temperature(unsigned t)const {
        const float charged=ceiling+afterheat;
        return t==0?charged:std::min(charged,cool(charged,t-1));
    }
    float terminal(float room)const {
        if(room>=in.target_c)return 0;
        const float hot=w.outdoor[n-1]+gain+heating_lift(n-1);
        if(hot<=in.target_c)return INFINITY;
        const float restore=tau*std::log((hot-room)/(hot-float(in.target_c)))+delay/60;
        // A positive restoration obligation is priced at the last observed
        // tariff. Do not award fictional negative-price terminal credits.
        return restore*std::max(0.0f,w.price[n-1]);
    }
    TariffCharge charge(unsigned s,float room,bool already_heating=false)const {
        TariffCharge r{};r.end=s;r.room=room;
        float remaining=already_heating?0:delay;
        for(unsigned j=s;j<n;++j) {
            const float eq=w.outdoor[j]+gain,hot=eq+heating_lift(j);
            if(hot<=ceiling+0.1f)return r;
            const float idle=std::min(float(tariff_step_minutes),remaining);
            remaining-=idle;
            if(idle>0)r.room=eq+(r.room-eq)*std::exp(-idle/(60*tau));
            const float productive=tariff_step_minutes-idle;
            if(r.room<floor-0.4f)return r;
            if(productive>0) {
                const float needed=r.room>=ceiling?0:
                    60*tau*std::log((hot-r.room)/(hot-ceiling));
                if(needed<=productive) {
                    // Price the actual (possibly partial) charging step.
                    r.cost+=(idle+std::max(0.0f,needed))/60*w.price[j];
                    r.end=j+1;
                    const float rest=productive-std::max(0.0f,needed);
                    r.room=eq+(ceiling+afterheat-eq)*std::exp(-rest/(60*tau));
                    r.valid=true;return r;
                }
                r.room=hot+(r.room-hot)*std::exp(-productive/(60*tau));
            }
            r.cost+=float(tariff_step_minutes)/60*w.price[j];
            r.end=j+1;
        }
        r.valid=r.room>=floor-0.3f;return r;
    }
    TariffSearch off(unsigned t,float room,unsigned dwell_left)const {
        TariffSearch r{};r.start=n;
        for(unsigned s=t;s<=n;++s) {
            if(s>t && room<floor-0.001f)break;
            if(s==n) {
                const float cost=terminal(room);
                if(cost<r.best){r.best=cost;r.start=n;}
                if(s>t)r.later=std::min(r.later,cost);
                break;
            }
            if(s-t>=dwell_left && (room<=ceiling-1.0f || room<=floor+0.25f)) {
                const auto c=charge(s,room);
                if(c.valid && c.end>s) {
                    const float tail=c.end==n?terminal(c.room):w.value[c.end];
                    const float cost=c.cost+tail;
                    if(cost<r.best-1e-6f){r.best=cost;r.start=s;}
                    if(s>t)r.later=std::min(r.later,cost);
                }
            }
            room=cool(room,s);
        }
        return r;
    }
    bool build() {
        w.value[n]=0;w.start[n]=static_cast<uint16_t>(n);
        // The full charge can finish anywhere inside the last 5-min step.
        // Use a conservative common OFF-node temperature, not the hottest
        // possible value, so the scheduler does not invent thermal storage.
        for(unsigned t=n;t-->0;) {
            const auto r=off(t,off_node_temperature(t),3);
            w.value[t]=r.best;w.start[t]=static_cast<uint16_t>(r.start);
        }
        return true;
    }
};
inline bool tariff_schedule(const MaintenanceTariffInput& in,size_t count,
        bool conservative,MaintenanceTariffAction& action,int64_t& next_start,
        double& advantage,double* proxy=nullptr) {
    if(proxy)*proxy=INFINITY;
    action=MaintenanceTariffAction::None;next_start=0;advantage=0;
    if(!in.scratch)return false;
    auto& w=*in.scratch;
    const auto& m=in.model;
    const auto horizon_end=in.prices[count-1].utc+900;
    unsigned n=static_cast<unsigned>(std::min<int64_t>(tariff_max_steps,
        (horizon_end-in.now)/(60*tariff_step_minutes)));
    for(unsigned t=0;t<n;++t) {
        const int64_t utc=in.now+int64_t(t)*60*tariff_step_minutes;
        const double price=maintenance_tariff_mean(in.prices,count,utc,utc+60*tariff_step_minutes);
        double outdoor=0;
        if(!std::isfinite(price)||!tariff_outdoor_at(in,utc,outdoor)){n=t;break;}
        w.price[t]=static_cast<float>(price);w.outdoor[t]=static_cast<float>(outdoor);
    }
    if(n<24)return false;
    TariffCycleSolver solver{in,w};
    solver.n=n;solver.tau=static_cast<float>(conservative?std::max(.5,m.tau_h-m.tau_std_h):m.tau_h);
    solver.decay=std::exp(-float(tariff_step_minutes)/(60*solver.tau));
    solver.gain=static_cast<float>(m.gain_rise_k-(conservative?std::max(.5,2*m.one_step_sigma_c):0));
    solver.lift=static_cast<float>(m.lift_k_ref-(conservative?m.lift_std_k:0));
    solver.lift_slope=static_cast<float>(m.lift_k_per_c);
    solver.lift_ref=static_cast<float>(m.lift_ref_outdoor_c);
    solver.delay=static_cast<float>(std::clamp(m.start_delay_min,0.,14.));
    solver.afterheat=static_cast<float>(conservative?0:std::clamp(m.afterheat_k,0.,1.));
    solver.floor=static_cast<float>(in.lower_c);solver.ceiling=static_cast<float>(in.cycle_ceiling_c);
    if(solver.ceiling-solver.floor<0.49f || solver.lift<=2)return false;
    solver.build();
    const unsigned remaining=in.minutes_in_mode>=cn105_low_temperature_dwell_minutes?0:
        (cn105_low_temperature_dwell_minutes-in.minutes_in_mode+tariff_step_minutes-1)/tariff_step_minutes;
    if(!in.heating) {
        const auto best=solver.off(0,static_cast<float>(in.room_c),remaining);
        if(!std::isfinite(best.best))return false;
        if(proxy)*proxy=best.best;
        if(best.start<n)next_start=in.now+int64_t(best.start)*60*tariff_step_minutes;
        if(best.start>0 && best.start<n && in.room_c>in.lower_c+0.2 &&
           w.price[best.start]+.002*in.price_scale<w.price[0]) {
            action=MaintenanceTariffAction::Coast;
            advantage=0;
        } else if(best.start==0 && remaining==0 &&
           in.room_c<=in.cycle_ceiling_c-1.0 && in.room_c>in.lower_c) {
            const float margin=std::max(.001f,float(.002*in.price_scale));
            if(!std::isfinite(best.later) || best.later-best.best>margin) {
                action=MaintenanceTariffAction::Preheat;
                advantage=std::isfinite(best.later)?best.later-best.best:0;
            }
        }
    } else if(remaining==0 && in.room_c>in.lower_c+0.2 &&
              in.room_c<in.cycle_ceiling_c) {
        const auto full=solver.charge(0,static_cast<float>(in.room_c),true);
        const auto stop=solver.off(0,static_cast<float>(in.room_c)+solver.afterheat,3);
        if(full.valid && std::isfinite(stop.best)) {
            const float continuing=full.cost+(full.end==n?solver.terminal(full.room):w.value[full.end]);
            const float restart=solver.delay/60*std::max(0.0f,w.price[0]);
            // Require the stopped pump to coast to a genuinely cheaper
            // start, and cover another measured warm-up before shortening.
            if(stop.start<n && stop.start>=3 && w.price[stop.start]<w.price[0] &&
               std::isfinite(continuing) && continuing-stop.best>restart+.002*in.price_scale) {
                action=MaintenanceTariffAction::Coast;
                next_start=in.now+int64_t(stop.start)*60*tariff_step_minutes;
                advantage=continuing-stop.best;
            }
        }
    }
    return true;
}
inline MaintenanceTariffAdvice maintenance_tariff_decision(const MaintenanceTariffInput& in) {
    MaintenanceTariffAdvice out{};
    out.computed_utc=in.now;
    if(!in.prices || in.count<2 || in.count>96 || in.now<=0 ||
       !std::isfinite(in.room_c)||!std::isfinite(in.target_c)||
       !std::isfinite(in.lower_c)||!std::isfinite(in.storage_ceiling_c)||
       !std::isfinite(in.cycle_ceiling_c)||!std::isfinite(in.price_scale)||
       in.price_scale<1 || in.price_scale>12 ||
       in.lower_c<5 || in.lower_c>in.target_c-0.5 ||
       in.cycle_ceiling_c<in.target_c || in.storage_ceiling_c<in.cycle_ceiling_c ||
       in.storage_ceiling_c>maintenance_model_ceiling_c)return out;
    const auto* p=in.prices;
    if(p[0].utc!=in.now/900*900)return out;
    size_t n=0;
    while(n<in.count && p[n].utc==p[0].utc+static_cast<int64_t>(n)*900 &&
          maintenance_tariff_price(p[n].eur_per_kwh,in.price_scale))++n;
    if(n<2)return out;
    out.valid=true;out.current_price=p[0].eur_per_kwh;out.price_end_utc=p[n-1].utc+900;
    out.ceiling_c=in.cycle_ceiling_c;
    // Protect costly periods from deliberate experimental short cycles.
    // Only use a cheaper two-slot window, not one noisy quarter.
    for(size_t i=1;i+1<n;++i) {
        const double cheaper=(p[i].eur_per_kwh+p[i+1].eur_per_kwh)/2;
        if(out.current_price-cheaper>
           std::max(0.005*in.price_scale,0.05*std::fabs(out.current_price))) {
            out.avoid_exploration=true;break;
        }
    }
    const double floor=in.lower_c;
    const double max_c=in.storage_ceiling_c;
    if(in.room_c<=floor || in.room_c>=max_c)return out; // real temperature wins
    const auto& m=in.model;
    const bool model=in.samples>=16 && in.off_samples>=4 && m.plausible &&
        std::isfinite(m.tau_h)&&std::isfinite(m.tau_std_h)&&m.tau_h>=0.5&&m.tau_h<=48 &&
        m.tau_std_h>=0&&m.tau_std_h<=0.75*m.tau_h &&
        std::isfinite(m.gain_rise_k)&&std::isfinite(m.lift_k_ref)&&m.lift_k_ref>2 &&
        std::isfinite(m.lift_std_k)&&m.lift_std_k>=0 &&
        std::isfinite(m.lift_k_per_c)&&std::isfinite(m.lift_ref_outdoor_c) &&
        std::isfinite(m.start_delay_min)&&std::isfinite(m.one_step_sigma_c)&&
        std::isfinite(in.outside_c)&&in.outside_c>=-55&&in.outside_c<=45;
    out.model_based=model;
    // Use a faster (one-standard-deviation) decay and cooler equilibrium
    // for judging reachable cheaper slots. Keep the configured floor intact.
    const double tau=model?std::max(0.5,m.tau_h-m.tau_std_h):1.0;
    const double equilibrium=model?in.outside_c+m.gain_rise_k-
        std::max(0.5,2*std::max(0.0,m.one_step_sigma_c)):floor-3;
    const double guard=floor+0.2;
    const double coast=model?maintenance_coast_hours(in.room_c,guard,equilibrium,tau):
        (in.room_c>=in.target_c+0.5?0.25:0);
    const double max_coast=model?maintenance_coast_hours(max_c,guard,equilibrium,tau):0.25;
    const int64_t deadline=in.now+static_cast<int64_t>(coast*3600);
    out.deadline_utc=deadline;
    const double margin=std::max(0.005*in.price_scale,0.05*std::fabs(out.current_price));
    // Prioritise cost before experimental cycle-size learning. Only
    // consider an early start when actual prices rise within the reachable
    // window, or an early stop if stored heat can bridge a cheaper window.
    MaintenanceTariffAction candidate=MaintenanceTariffAction::None;
    int64_t alternative=0;double alternative_price=0;
    if((!in.heating || in.minutes_in_mode>=cn105_low_temperature_dwell_minutes) &&
       in.room_c>floor+0.2) {
        for(size_t i=1;i+1<n && p[i].utc<=deadline;++i) {
            if(p[i].eur_per_kwh+margin<out.current_price &&
               p[i+1].eur_per_kwh+margin<out.current_price) {
                candidate=MaintenanceTariffAction::Coast;alternative=p[i].utc;
                alternative_price=(p[i].eur_per_kwh+p[i+1].eur_per_kwh)/2;break;
            }
        }
    }
    if(candidate==MaintenanceTariffAction::None && !in.heating &&
       in.minutes_in_mode>=cn105_low_temperature_dwell_minutes && max_c-in.room_c>=1.0) {
        const int64_t latest=in.now+static_cast<int64_t>(
            std::max(0.25,std::min(max_coast,4.0))*3600);
        for(size_t i=1;i<n && p[i].utc<=latest;++i) {
            if(p[i].eur_per_kwh>out.current_price+margin) {
                candidate=MaintenanceTariffAction::Preheat;alternative=p[i].utc;
                alternative_price=p[i].eur_per_kwh;break;
            }
        }
    }
    if(candidate==MaintenanceTariffAction::None)return out;
    // Do not spend energy on an unvalidated full-storage experiment.
    // A valid price alone cannot prove the building's thermal response.
    if(!model)return out;
    MaintenanceTariffAction nominal=MaintenanceTariffAction::None,robust=MaintenanceTariffAction::None;
    int64_t nominal_start=0,robust_start=0;double nominal_adv=0,robust_adv=0;
    double nominal_proxy=INFINITY,robust_proxy=INFINITY;
    bool first_ok=tariff_schedule(in,n,false,nominal,nominal_start,nominal_adv,&nominal_proxy);
    bool second_ok=tariff_schedule(in,n,true,robust,robust_start,robust_adv,&robust_proxy);
    double selected_ceiling=in.cycle_ceiling_c;
    // The learned ordinary cycle can be short. Also evaluate the allowed
    // storage alternative when a price rise is actually reachable. Compare
    // equal horizons and restoration targets (or reject an infeasible
    // narrow schedule) using the existing thermal
    // runtime-price proxy, not invented kWh or euro savings.
    if(candidate==MaintenanceTariffAction::Preheat && max_c-in.cycle_ceiling_c>=0.5) {
        auto storage=in;storage.cycle_ceiling_c=max_c;
        MaintenanceTariffAction alt_nominal=MaintenanceTariffAction::None,alt_robust=MaintenanceTariffAction::None;
        int64_t alt_start=0,alt_robust_start=0;
        double alt_adv=0,alt_robust_adv=0,alt_proxy=INFINITY,alt_robust_proxy=INFINITY;
        const bool alt_ok=tariff_schedule(storage,n,false,alt_nominal,alt_start,alt_adv,&alt_proxy);
        const bool alt_robust_ok=tariff_schedule(storage,n,true,alt_robust,alt_robust_start,alt_robust_adv,&alt_robust_proxy);
        const double proxy_margin=0.002*in.price_scale;
        if(alt_ok && alt_robust_ok && alt_nominal==candidate && alt_robust==candidate &&
           (!first_ok || alt_proxy+proxy_margin<nominal_proxy) &&
           (!second_ok || alt_robust_proxy+proxy_margin<robust_proxy)) {
            nominal=alt_nominal;robust=alt_robust;
            nominal_start=alt_start;robust_start=alt_robust_start;
            selected_ceiling=max_c;first_ok=second_ok=true;
        }
    }
    if(first_ok && second_ok && nominal_start>0 && robust_start>0)
        out.next_start_utc=std::min(nominal_start,robust_start);
    if(first_ok && second_ok && nominal==candidate && robust==candidate) {
        out.action=candidate;out.avoid_exploration=true;
        out.alternative_utc=alternative;out.alternative_price=alternative_price;
        // Full-charge scheduling uses the learned economical cycle ceiling.
        // The live temperature rule still decides the physical stop.
        out.ceiling_c=candidate==MaintenanceTariffAction::Preheat?selected_ceiling:
            std::clamp(in.room_c,in.lower_c+0.5,in.cycle_ceiling_c);
        out.required_room_c=out.ceiling_c;
    }
    return out;
}
} // namespace enevina::cottage
