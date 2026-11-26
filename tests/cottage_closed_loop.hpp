#pragma once
// Independent closed-loop comparison harness (host only, SIMULATION).
//
// A "true" building + heat pump (cottage/physics.hpp advance() with its own
// parameters, after-heat, start delay, a time-varying solar gain the models
// do not know, sensor noise and outdoor-forecast error) is driven by three
// controllers on identical weather, prices and initial state:
//   * thermostat          : room target +/- 0.5 C, 30-minute mode dwell
//   * Lite price control  : the existing bootstrap_price_decision()
//   * calibrated MPC      : BuildingIdentifier (learned from the plant's own
//                           measurements) -> plan_mpc() -> arbitrate() ->
//                           price control -> thermostat, re-planned every
//                           slot, only the first decision executed.
// Nothing here is a device test or a field result.
#include "cottage/building_identification.hpp"
#include "cottage/finnish_tariff.hpp"
#include "cottage/forecast_bridge.hpp"
#include "cottage/mpc_arbiter.hpp"
#include "cottage/mpc_planner.hpp"
#include "cottage/price_bootstrap.hpp"
#include "forecast/pricing.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace closed_loop {
using namespace enevina::cottage;

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed):s(seed*6364136223846793005ULL+1442695040888963407ULL){}
    double uniform() {
        s=s*6364136223846793005ULL+1442695040888963407ULL;
        return ((s>>11)+0.5)*(1.0/9007199254740992.0);
    }
    double normal() {
        const double u=uniform(),v=uniform();
        return std::sqrt(-2.0*std::log(u))*std::cos(6.283185307179586*v);
    }
};

inline Parameters true_cottage() {
    Parameters p{};
    p.thermal_mass_kwh_per_c=2.0;
    p.loss_kw_per_c=0.12;
    p.heat_kw_at_zero_c=3.2;
    p.heat_kw_change_per_outdoor_c=0.03;
    p.minimum_heat_kw=1.0;
    p.cop_at_zero_c=2.8;
    p.cop_change_per_outdoor_c=0.04;
    p.minimum_cop=1.2;p.maximum_cop=5.0;
    p.heat_standby_kw=0.012;p.off_standby_kw=0.002;
    p.startup_electric_kwh=0.05;
    p.startup_delay_minutes=3.0;
    p.stop_afterheat_kwh=0.05;
    p.planning_min_c=5.0;p.critical_c=5.0;
    p.maximum_indoor_c=16.0;
    p.min_heat_minutes=15;p.min_off_minutes=15;
    return p;
}

struct Scenario {
    std::string name, description;
    int64_t t0=0;                 // evaluation start (UTC, 00:00 local)
    size_t learn_slots=14*96;     // history learned under price control
    size_t eval_slots=3*96;
    double target_c=12.0;
    double initial_c=12.0;
    Parameters plant=true_cottage();
    double base_gain_kw=0.10, solar_peak_kw=0.30;
    // Per-slot data covering learn + eval (index 0 = t0 - learn_slots*900).
    std::vector<double> outdoor, spot;
    // Outages (slot index ranges inside the evaluation window).
    size_t sensor_out_begin=0, sensor_out_end=0;
    size_t weather_out_begin=0, weather_out_end=0;
    size_t price_out_begin=0, price_out_end=0;
    // Identifier prior: a deliberately wrong capacity/lift guess.
    bool wrong_prior=false;
    bool meter_pump_only=false;
};

inline enevina::Tariff tariff_for(int64_t utc,double spot) {
    bool day=false;
    finnish_day_transfer(utc,day);
    enevina::Tariff t{};
    t.spot_eur_per_kwh=spot;
    t.margin_eur_per_kwh=0.0049;
    t.transfer_eur_per_kwh=day?0.0345:0.0210;
    t.electricity_tax_eur_per_kwh=0.02827515;
    t.vat_rate=0.255;
    t.electricity_tax_includes_vat=true;
    return t;
}
inline double total_price(int64_t utc,double spot) {
    double total=0;
    enevina::calculate_total_price(tariff_for(utc,spot),total);
    return total;
}
inline bool sim_pricer(void*,int64_t utc,double spot,double& total) {
    return enevina::calculate_total_price(tariff_for(utc,spot),total);
}

// Day-ahead availability: prices for the next local day appear at 12:00 UTC
// (~14 EET); before that only today's local day is known.
inline int64_t prices_known_until(int64_t now) {
    const int64_t local=now+2*3600;              // EET (scenarios avoid DST)
    const int64_t day_end=(local/86400+1)*86400-2*3600;
    const int64_t local_hour=(local%86400)/3600;
    return local_hour>=14?day_end+86400:day_end;
}

struct Metrics {
    std::string controller;
    double cost=0, electric_kwh=0, thermal_kwh=0;
    double final_c=0, min_c=1e9, max_c=-1e9;
    double below_kh=0, above_kh=0;   // degree-hours outside the comfort band
    unsigned violation_slots=0, starts=0;
    double equalized_cost=0;         // cost + energy to return to the initial state
    unsigned mpc_active=0, mpc_attempts=0, solver_failures=0;
    unsigned learned_range_limited_plans=0, invalid_learned_range_plans=0;
    unsigned calibrated_at_slot=0;   // 0 = calibrated before evaluation
    bool calibrated_at_end=false;
    double solve_ms_max=0, solve_ms_sum=0;
    std::map<std::string,unsigned> fallbacks;
    std::map<std::string,unsigned> solver_status;
    std::string calibration_state;
};

enum class Kind { Thermostat, Price, Mpc };
inline const char* mpc_status_label(MpcStatus s) {
    switch(s) {
    case MpcStatus::Ok:return "OK";
    case MpcStatus::InvalidInput:return "INVALID_INPUT";
    case MpcStatus::UnsafeStart:return "UNSAFE_START";
    case MpcStatus::Infeasible:return "INFEASIBLE";
    case MpcStatus::ResourceLimit:return "RESOURCE_LIMIT";
    case MpcStatus::ReplayFailed:return "REPLAY_FAILED";
    case MpcStatus::Timeout:return "TIMEOUT";
    case MpcStatus::Cancelled:return "CANCELLED";
    }
    return "UNKNOWN";
}

class Harness {
public:
    explicit Harness(const Scenario& s):s_(s),rng_(0xC0FFEEu) {}
    size_t total_slots() const {return s_.learn_slots+s_.eval_slots;}
    int64_t slot_utc(size_t i) const {
        return s_.t0-static_cast<int64_t>(s_.learn_slots)*900+static_cast<int64_t>(i)*900;
    }
    // Forecast issued every 6 h; error grows linearly with lead (deterministic).
    double forecast(size_t issue_slot,size_t target_slot) const {
        const double lead_h=(static_cast<double>(target_slot)-issue_slot)*0.25;
        Rng r(1000+issue_slot);
        const double amp=1.5*r.normal();
        return s_.outdoor[std::min(target_slot,s_.outdoor.size()-1)]+
            amp*std::min(1.0,std::max(0.0,lead_h)/24.0);
    }
    size_t last_issue(size_t i) const {return (i/24)*24;}
    bool in(size_t i,size_t b,size_t e) const {
        return i>=s_.learn_slots+b && i<s_.learn_slots+e && e>b;
    }

    Metrics run(Kind kind,BuildingIdentifier* id_out=nullptr) {
        Metrics m{};
        m.controller=kind==Kind::Thermostat?"thermostat":kind==Kind::Price?
            "lite_price":"calibrated_mpc";
        BuildingIdentifier id;
        if(s_.wrong_prior) id.set_prior(4*16.67,3*26.0,50.0);
        Parameters plant=s_.plant;
        State st{s_.initial_c,false,120};
        Rng noise(77);
        Rng meter_noise(91);  // separate: sensor noise identical for all controllers
        uint32_t since_change=120;
        const size_t n=total_slots();
        double t_meas=st.indoor_c+0.03*noise.normal();
        bool calibrated_logged=false;
        for(size_t i=0;i<n;++i) {
            const bool eval=i>=s_.learn_slots;
            if(i==s_.learn_slots) {
                // Identical initial state for every controller.
                st={s_.initial_c,false,120};since_change=120;
                t_meas=st.indoor_c+0.03*noise.normal();
                if(kind==Kind::Mpc && id.calibrated(slot_utc(i))) calibrated_logged=true;
            }
            const int64_t now=slot_utc(i);
            const bool sensor_ok=!in(i,s_.sensor_out_begin,s_.sensor_out_end);
            const bool weather_ok=!in(i,s_.weather_out_begin,s_.weather_out_end);
            const bool prices_ok=!in(i,s_.price_out_begin,s_.price_out_end);
            // --- price controller inputs (next 8 slots) ---
            BootstrapInput bi{};bi.heating={16,26,0.5,true,true};
            bi.indoor_fresh=sensor_ok;
            bi.indoor_c=t_meas;
            bi.desired_room_c=s_.target_c;
            std::array<double,8> next{};
            const bool full=prices_ok && i+8<=s_.spot.size() &&
                slot_utc(i+7)<prices_known_until(now);
            for(size_t k=0;full && k<8;++k) next[k]=total_price(slot_utc(i+k),s_.spot[i+k]);
            bi.next_total_prices=next.data();bi.price_count=8;
            bi.prices_contiguous_and_fresh=full;
            const BootstrapDecision boot=bootstrap_price_decision(bi);
            BootstrapDecision proposal=boot;
            if(kind==Kind::Thermostat) {
                BootstrapInput ti=bi;ti.prices_contiguous_and_fresh=false;
                proposal=bootstrap_price_decision(ti);
            } else if(kind==Kind::Mpc && eval) {
                proposal=mpc_proposal(id,i,now,t_meas,sensor_ok,weather_ok,prices_ok,
                                      st,since_change,boot,m);
            }
            bool want=st.heat_requested;
            if(proposal.action==BootstrapAction::HeatSetpoint) want=true;
            else if(proposal.action==BootstrapAction::Off) want=false;
            // Owner rule: >= 30 min between HEAT<->OFF changes.
            if(want!=st.heat_requested && since_change<30) want=st.heat_requested;
            const bool started=want && !st.heat_requested;
            // --- plant step (truth) ---
            const double solar=s_.solar_peak_kw*std::max(0.0,
                std::sin(6.283185307179586*(((now+2*3600)%86400)/86400.0-0.25)));
            Period actual{s_.outdoor[i],total_price(now,s_.spot[i]),
                          s_.base_gain_kw+solar,1.0};
            const State before=st;
            const double t_start_meas=t_meas;
            const Step step=advance(plant,st,actual,want);
            st=step.next;
            since_change=want!=before.heat_requested?15:std::min(600u,since_change+15);
            t_meas=std::round((st.indoor_c+0.03*noise.normal())*100.0)/100.0;
            // --- learning (MPC pipeline only; same as firmware slot close) ---
            if(kind==Kind::Mpc) {
                SlotObservation o{};
                o.slot_utc=now;
                o.indoor_valid=sensor_ok && !in(i+1,s_.sensor_out_begin,s_.sensor_out_end);
                o.indoor_start_c=t_start_meas;o.indoor_end_c=t_meas;
                o.outdoor_valid=weather_ok;
                o.outdoor_c=forecast(last_issue(i),i);
                // The learning history and the evaluation are joined by a
                // state reset (identical initial state for all controllers):
                // tell the identifier, as a reboot would, that continuity broke.
                o.heat_valid=i!=s_.learn_slots;o.heat_requested=want;
                o.in_model_domain=true;
                o.electric_valid=true;
                o.electric_kwh=std::max(0.0,step.electric_kwh*(1.0+0.02*meter_noise.normal()));
                o.meter_pump_only=s_.meter_pump_only;
                id.add(o);
                if(eval && !calibrated_logged && id.calibrated(now+900)) {
                    calibrated_logged=true;m.calibrated_at_slot=static_cast<unsigned>(i-s_.learn_slots+1);
                }
            }
            if(!eval) continue;
            if(trace_)
                std::fprintf(trace_,"%zu,%lld,%.3f,%.2f,%d,%.4f,%s\n",i-s_.learn_slots,
                    static_cast<long long>(now),st.indoor_c,s_.outdoor[i],want?1:0,
                    total_price(now,s_.spot[i]),last_source_);
            if(started) m.starts++;
            m.cost+=step.cost_eur;m.electric_kwh+=step.electric_kwh;
            m.thermal_kwh+=step.delivered_thermal_kw*slot_hours;
            m.min_c=std::min(m.min_c,st.indoor_c);m.max_c=std::max(m.max_c,st.indoor_c);
            const double lo=s_.target_c-0.5-0.1,hi=std::min(15.8,s_.target_c+1.0)+0.1;
            if(st.indoor_c<lo) {m.below_kh+=(lo-st.indoor_c)*slot_hours;m.violation_slots++;}
            if(st.indoor_c>hi) {m.above_kh+=(st.indoor_c-hi)*slot_hours;m.violation_slots++;}
        }
        m.final_c=st.indoor_c;
        // Equal final thermal energy: price the electricity needed (true COP at
        // the final outdoor temperature, mean evaluation price) to bring the
        // room back to the common initial temperature (negative = credit).
        double mean_price=0;
        for(size_t i=s_.learn_slots;i<n;++i) mean_price+=total_price(slot_utc(i),s_.spot[i]);
        mean_price/=static_cast<double>(s_.eval_slots);
        const double dq=plant.thermal_mass_kwh_per_c*(s_.initial_c-m.final_c);
        m.equalized_cost=m.cost+dq/heat_cop(plant,s_.outdoor[n-1])*mean_price;
        if(kind==Kind::Mpc) {
            const auto r=id.report(slot_utc(n));
            m.calibration_state=calibration_state_name(r.state);
            m.calibrated_at_end=r.state==CalibrationState::Calibrated;
            if(!calibrated_logged) m.calibrated_at_slot=static_cast<unsigned>(-1);
        }
        if(id_out) *id_out=id;
        return m;
    }

private:
    BootstrapDecision mpc_proposal(const BuildingIdentifier& id,size_t i,int64_t now,
        double t_meas,bool sensor_ok,bool weather_ok,bool prices_ok,const State& st,
        uint32_t since_change,const BootstrapDecision& boot,Metrics& m) {
        // Price cache as the firmware sees it: known slots, possibly missing.
        std::vector<Spot15> prices;
        const int64_t known=prices_known_until(now);
        if(prices_ok)
            for(size_t k=i;k<s_.spot.size() && prices.size()<200;++k) {
                if(slot_utc(k)>=known) break;
                prices.push_back({slot_utc(k),s_.spot[k]});
            }
        // Hourly MET-style forecast from the latest issue (<= 6 h old).
        std::vector<enevina::OutdoorPoint> weather;
        const size_t issue=last_issue(i);
        if(weather_ok) {
            const int64_t issue_utc=slot_utc(issue);
            for(size_t h=0;h<64;++h) {
                const size_t slot=issue+h*4;
                weather.push_back({issue_utc+static_cast<int64_t>(h)*3600,
                                   forecast(issue,std::min(slot,s_.outdoor.size()-1))});
            }
        }
        HorizonResult hz{};
        if(!prices.empty() && weather.size()>=2)
            hz=join_forecast_horizon(now,now-600,slot_utc(issue),prices.data(),
                prices.size(),weather.data(),weather.size(),sim_pricer,nullptr,192,
                mpc_min_horizon_slots);
        PlannerInput pi{};pi.pump_limits={16,26,0.5,true,true};
        pi.now_utc=now;pi.identifier=&id;pi.meter_pump_only=s_.meter_pump_only;
        pi.currency_scale=1.0;
        pi.heating_allowed=true;pi.target_valid=true;pi.target_c=s_.target_c;
        pi.temperature_fresh=sensor_ok;pi.indoor_c=t_meas;
        pi.pump_state_known=true;pi.pump_heat=st.heat_requested;
        // Firmware view of dwell: time since the last observed change.
        pi.minutes_in_mode=since_change;
        pi.horizon=(prices.empty()||weather.size()<2)?nullptr:&hz;
        pi.controls.runtime_budget_ms=60000;
        const bool calibrated=id.calibrated(now);
        PlannerOutput po{};
        double ms=0;
        if(calibrated) {
            const auto t0=std::chrono::steady_clock::now();
            po=plan_mpc(pi,nullptr);
            ms=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-t0).count();
            if(po.record.status!=MpcStatus::InvalidInput || po.record.has_plan) {
                m.mpc_attempts++;
                m.solve_ms_max=std::max(m.solve_ms_max,ms);m.solve_ms_sum+=ms;
                // UNSAFE_START = the measured room is below the planning floor;
                // handled by the fallback, it is not a solver failure.
                if(po.record.status!=MpcStatus::Ok &&
                   po.record.status!=MpcStatus::UnsafeStart) {
                    m.solver_failures++;
                    m.solver_status[mpc_status_label(po.record.status)]++;
                }
            }
        }
        ArbiterInput ai{};ai.pump_limits={16,26,0.5,true,true};
        ai.now_utc=now;ai.temperature_fresh=sensor_ok;ai.indoor_c=t_meas;
        const auto rep=id.report(now);
        ai.calibrated=rep.state==CalibrationState::Calibrated;
        ai.calibration=rep.state;
        ai.plan=po.record;
        ai.pump_state_known=true;ai.pump_heat=st.heat_requested;
        ai.bootstrap=boot;
        if(!calibrated) ai.plan.has_plan=false;
        const ArbiterOutput out=arbitrate(ai);
        if(out.source==ProposalSource::Mpc) {
            m.mpc_active++;
            if(hz.limit==HorizonLimit::LearnedOutdoorRange)++m.learned_range_limited_plans;
            const auto estimate=id.report(now).model;
            for(size_t k=0;k<po.record.count;++k) {
                const double to=hz.bridge.periods[k].outdoor_c;
                if(!std::isfinite(to) || to<estimate.outdoor_min_c-CalibrationLimits::outdoor_margin_c ||
                   to>estimate.outdoor_max_c+CalibrationLimits::outdoor_margin_c)
                    ++m.invalid_learned_range_plans;
            }
        } else m.fallbacks[mpc_fallback_name(out.fallback)]++;
        last_source_=out.source==ProposalSource::Mpc?"MPC":mpc_fallback_name(out.fallback);
        return out.proposal;
    }
public:
    void set_trace(std::FILE* f) {trace_=f;}
private:
    const Scenario& s_;
    Rng rng_;
    std::FILE* trace_=nullptr;
    const char* last_source_="-";
};

// ----- scenario catalogue ------------------------------------------------
// Every series covers learning + evaluation + 48 h, so the evaluation's last
// slots still see a full forecast horizon (no artificial end-of-data effect).
constexpr size_t tail_slots=192;
inline double base_spot(size_t i) {
    // Typical winter day-ahead shape in EUR/kWh (local EET hours).
    const size_t local_slot=(i+8)%96;           // +2 h
    const double hour=local_slot/4.0;
    double v=0.06+0.05*std::sin(6.283185307179586*(hour-6)/24.0);
    if(hour>=7 && hour<10) v+=0.08;             // morning peak
    if(hour>=17 && hour<21) v+=0.10;            // evening peak
    if(hour<5) v-=0.03;                         // cheap night
    return v;
}
inline double base_outdoor(size_t i,double mean,double swing) {
    const double hour=((i+8)%96)/4.0;
    return mean+swing*std::sin(6.283185307179586*(hour-9)/24.0)+
        1.5*std::sin(6.283185307179586*i/(96*3.7));
}
inline size_t local_hour(size_t i) {return ((i+8)%96)/4;}
inline void fill(Scenario& s) {
    const size_t n=s.learn_slots+s.eval_slots+tail_slots;
    s.outdoor.resize(n);s.spot.resize(n);
    for(size_t i=0;i<n;++i) {s.outdoor[i]=base_outdoor(i,-4.0,4.0);s.spot[i]=base_spot(i);}
}
inline std::vector<Scenario> scenarios() {
    std::vector<Scenario> out;
    // 2026-01-12 00:00 EET = 2026-01-11 22:00 UTC (no DST change in Jan/Feb).
    const int64_t t0=1768168800;
    const auto make=[&](const char* name,const char* desc) {
        Scenario s;s.name=name;s.description=desc;s.t0=t0;fill(s);return s;
    };
    // k runs over the evaluation AND the tail (what the forecast can see).
    const auto each_eval=[](Scenario& s,auto f) {
        for(size_t k=0;k<s.eval_slots+tail_slots;++k) f(s.learn_slots+k,k);
    };
    {auto s=make("cold_spell","Outdoor falls from -4 C to about -11 C over 12 h on day 2 "
                 "(pump near its limit); learned range -10..+2 C");
     each_eval(s,[&](size_t i,size_t k){
         const double ramp=k<96?0.0:std::min(1.0,(k-96)/48.0);
         s.outdoor[i]=base_outdoor(i,-4.0,4.0)*(1-ramp)+(-11.0+1.0*std::sin(6.283*k/96.0))*ramp;});
     out.push_back(s);}
    {auto s=make("fast_warmup","Room starts 3 K below target (arrival after a cold period)");
     s.initial_c=9.0;out.push_back(s);}
    {auto s=make("price_spike","Evening spike 1.2 EUR/kWh 17-20 local on day 2");
     each_eval(s,[&](size_t i,size_t k){if(k>=96+68 && k<96+80) s.spot[i]=1.2;});
     out.push_back(s);}
    {auto s=make("cheap_night_expensive_morning","0.01 EUR/kWh 00-05, 0.45 EUR/kWh 06-10 every day");
     each_eval(s,[&](size_t i,size_t){const size_t h=local_hour(i);
         s.spot[i]=h<5?0.01:(h>=6&&h<10)?0.45:0.08;});
     out.push_back(s);}
    {auto s=make("negative_prices","Spot -0.05 EUR/kWh 10-15 local every day");
     each_eval(s,[&](size_t i,size_t){const size_t h=local_hour(i);
         if(h>=10&&h<15) s.spot[i]=-0.05;});
     out.push_back(s);}
    {auto s=make("flat_prices","Constant 0.10 EUR/kWh spot");
     each_eval(s,[&](size_t i,size_t){s.spot[i]=0.10;});
     out.push_back(s);}
    {auto s=make("undersized_pump","Pump capacity 1.4 kW at 0 C; nights down to -14 C");
     s.plant.heat_kw_at_zero_c=1.4;s.plant.heat_kw_change_per_outdoor_c=0.02;
     s.plant.minimum_heat_kw=0.6;
     each_eval(s,[&](size_t i,size_t k){
         s.outdoor[i]=-6.0-8.0*std::max(0.0,std::sin(6.283*(k%96)/96.0));});
     out.push_back(s);}
    {auto s=make("sensor_outage","Indoor sensor missing 6 h on day 2");
     s.sensor_out_begin=96+24;s.sensor_out_end=96+48;out.push_back(s);}
    {auto s=make("weather_price_outage","Weather stale 12 h on day 1, prices missing 12 h on day 2");
     s.weather_out_begin=24;s.weather_out_end=72;
     s.price_out_begin=96+24;s.price_out_end=96+72;out.push_back(s);}
    {Scenario s;s.name="wrong_initial_guess";
     s.description="Identifier prior 4x time constant, 3x heating lift; only 5 learning days";
     s.t0=t0;s.wrong_prior=true;s.learn_slots=5*96;fill(s);out.push_back(s);}
    {auto s=make("price_spike_pump_meter","As price_spike, with an owner-confirmed heat-pump-only meter");
     each_eval(s,[&](size_t i,size_t k){if(k>=96+68 && k<96+80) s.spot[i]=1.2;});
     s.meter_pump_only=true;out.push_back(s);}
    return out;
}
} // namespace closed_loop
