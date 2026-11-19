// Builds a realistic HISTORY through the real pipeline and prints /api/history JSON:
// simulated room/pump (synthetic building, real recorded prices) -> HistoryRecorder
// -> HistoryStore -> HistoryPersistence (real file, reloaded) -> query -> history_json.
// Used by tools/dev_device_simulator.py; shows deliberate outages and a forecast-only
// outdoor period so the UI's gap/provenance handling can be seen.
// usage: history_cli <prices.csv> <range 24h|7d|30d> <scratch dir>
#include "../tests/profile_sim.hpp"
#include "cottage/history_persist.hpp"
#include "cottage/history_query.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace enevina::cottage;

struct StdioFile final : FileBackend {
    std::string path,tmp;
    explicit StdioFile(const std::string& p):path(p),tmp(p+".tmp") {}
    bool exists() override {std::FILE* f=std::fopen(path.c_str(),"rb");if(f) std::fclose(f);return f!=nullptr;}
    size_t size() override {
        std::FILE* f=std::fopen(path.c_str(),"rb");
        if(!f) return 0;
        std::fseek(f,0,SEEK_END);const long n=std::ftell(f);std::fclose(f);return n<0?0:static_cast<size_t>(n);
    }
    bool read(size_t off,void* out,size_t n) override {
        std::FILE* f=std::fopen(path.c_str(),"rb");
        if(!f) return false;
        const bool ok=std::fseek(f,static_cast<long>(off),SEEK_SET)==0&&std::fread(out,1,n,f)==n;
        std::fclose(f);return ok;
    }
    bool append(const void* d,size_t n) override {
        std::FILE* f=std::fopen(path.c_str(),"ab");
        if(!f) return false;
        const bool ok=std::fwrite(d,1,n,f)==n;std::fclose(f);return ok;
    }
    std::FILE* t=nullptr;
    bool replace_begin() override {t=std::fopen(tmp.c_str(),"wb");return t!=nullptr;}
    bool replace_write(const void* d,size_t n) override {return t&&std::fwrite(d,1,n,t)==n;}
    bool replace_commit() override {
        if(!t) return false;
        std::fclose(t);t=nullptr;
        std::remove(path.c_str());
        return std::rename(tmp.c_str(),path.c_str())==0;
    }
    void replace_abort() override {if(t) std::fclose(t);t=nullptr;std::remove(tmp.c_str());}
};

int main(int argc,char** argv) {
    if(argc!=4) {std::fprintf(stderr,"usage: history_cli <prices.csv> <range> <scratch dir>\n");return 2;}
    profile_sim::Series s;
    if(!profile_sim::load_series(argv[1],s)) return 1;
    RangeSpec spec{};
    if(!history_range_spec(argv[2],spec)) return 2;
    const std::string dir=argv[3];
    StdioFile rf(dir+"/hist.bin"),ef(dir+"/events.bin");
    std::remove((dir+"/hist.bin").c_str());std::remove((dir+"/events.bin").c_str());

    HistoryStore store(kHistorySlots);EventLog events;HistoryRecorder rec;
    HistoryPersistence persist(rf,ef);
    persist.load(store,events);

    const Parameters pm=profile_sim::pump();
    const profile_sim::Building b;
    double T=b.start_c,setpoint=b.target_c;
    bool on=false;
    uint32_t sent=0,confirmed=0;
    const size_t n=s.per_mwh.size();
    for(size_t t=0;t+16<n;++t) {
        // controller proposal for this slot (Balanced, limits 19..23), as on the device
        BootstrapInput in{};
        in.indoor_fresh=true;in.indoor_c=T;in.desired_room_c=b.target_c;
        size_t window=8;
        apply_profile(in,OptimizationProfile::Balanced,19,23,0.10,window);
        double totals[16];
        for(size_t i=0;i<window;++i) totals[i]=s.per_mwh[t+i]/1000.0+0.06;
        in.next_total_prices=totals;in.price_count=window;in.prices_contiguous_and_fresh=true;
        const BootstrapDecision d=bootstrap_price_decision(in);
        const bool prev=on;
        if(d.action==BootstrapAction::Off) on=false;
        else if(d.action==BootstrapAction::HeatSetpoint) {on=true;setpoint=d.heat_setpoint_c;}
        const double out=profile_sim::outdoor_c(t);
        double q=0;
        if(on) {
            const double need=(std::min(setpoint,24.0)-T)*b.mass_kwh_per_c/slot_hours+b.loss_kw_per_c*(T-out);
            q=std::max(0.0,std::min(need,heat_capacity_kw(pm,out)));
        }
        const double electric_kw=q>0?q/heat_cop(pm,out)+0.012:0.002;
        if(on!=prev) {++sent;++confirmed;}
        // three samples per slot, like a 15 s cadence thinned for the tool
        const double hours_from_start=static_cast<double>(t)/4.0;
        const bool wifi_down=hours_from_start>=60&&hours_from_start<65;           // 5 h Wi-Fi outage
        const bool no_meter=hours_from_start>=96&&hours_from_start<120;           // meter offline a day
        const bool forecast_only=hours_from_start<72;                              // outdoor sensor added later
        const bool offline_gap=hours_from_start>=130&&hours_from_start<134;       // 4 h power cut: nothing recorded at all
        for(int k=0;k<3;++k) {
            HistoryInputs h;
            h.clock_trusted=true;
            h.utc=s.utc[t]+k*300+5;
            h.wifi_ok=!wifi_down;h.pump_online=true;h.sensor_online=true;h.prices_ok=true;
            h.indoor_valid=true;h.indoor_c=T;
            h.rh_valid=true;h.rh_pct=42+3*std::sin(hours_from_start/5);
            h.outdoor_valid=true;h.outdoor_c=out+(forecast_only?0.8:0.0);h.outdoor_is_forecast=forecast_only;
            h.setpoint_valid=on;h.setpoint_c=setpoint;
            h.power_valid=!no_meter;h.power_w=electric_kw*1000.0;
            h.price_valid=true;h.price_per_kwh=totals[0];
            h.action=d.action==BootstrapAction::Off?1:d.action==BootstrapAction::HeatSetpoint?2:3;
            h.armed=hours_from_start>=24;
            h.commands_sent=sent;h.commands_confirmed=confirmed;
            h.reset_reason=1;
            if(!offline_gap) rec.observe(h,store,events);
            else if(k==0) {HistoryInputs lost=h;lost.clock_trusted=false;rec.observe(lost,store,events);}
        }
        T+=(q-b.loss_kw_per_c*(T-out))*slot_hours/b.mass_kwh_per_c;
        if(t%4==3) persist.flush(store,events);
    }
    persist.flush(store,events);
    // Reload from the file the way a restart would, and answer from THAT.
    HistoryStore reloaded(kHistorySlots);EventLog ev2;HistoryPersistence p2(rf,ef);
    const LoadResult lr=p2.load(reloaded,ev2);
    if(!lr.record_header_ok) {std::fprintf(stderr,"reload failed\n");return 1;}
    std::vector<HistoryPoint> pts(kMaxHistoryPoints);
    HistorySummary sum;
    const int64_t end=(static_cast<int64_t>(reloaded.newest_slot())+1)*900-1;
    const size_t m=build_history_series(reloaded,end,spec.range_slots,spec.step_slots,pts.data(),pts.size(),sum);
    history_json(spec.name,spec.step_slots,pts.data(),m,sum,ev2,m?pts[0].t:0,"EUR","unknown",true,
        [](const char* part){std::fputs(part,stdout);});
    std::fputc('\n',stdout);
    return 0;
}
