#include "physics/physics.hpp"
#include "optimizer/optimizer.hpp"
#include "physics/calibration.hpp"
#include "forecast/pricing.hpp"
#include "forecast/validity.hpp"
#include "forecast/sources.hpp"
#include "forecast/fmi.hpp"
#include "safety/supervisor.hpp"
#include "pump/heishamon_adapter.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace enevina;

void close(double actual, double expected, double tolerance = 1e-6) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}
void require(bool value, const char* reason) {
    if (!value) { std::cerr << reason << '\n'; std::exit(1); }
}

int main() {
    Parameters p{};
    p.building_capacity_kwh_per_c = 10;
    p.building_loss_kw_per_c = 0.2;
    p.tank_capacity_kwh = 8;
    p.tank_loss_kw_per_kwh = 0;
    p.pump_max_thermal_kw = 8;
    p.cop_at_reference = 3;
    p.cop_outdoor_per_c = 0.05;
    p.cop_supply_per_c = 0.03;
    p.minimum_cop = 1;
    p.maximum_cop = 6;
    p.emitter_kw_per_c = 1;
    p.dhw_supply_c = 50;
    p.indoor_min_c = 19;
    p.indoor_max_c = 22;
    p.tank_min_kwh = 2;

    close(cop(p, 0, 35), 3);
    close(cop(p, -10, 45), 2.2);
    close(cop(p, -100, 90), 1);

    State s{20, 6};
    Period d{0, 0.3, 0, 2, 0};
    auto idle = advance(p, s, d, Action{Mode::Idle, 0, 35});
    close(idle.next.indoor_c, 19.9);
    close(idle.next.tank_kwh, 5.5);
    close(idle.electric_kwh, 0);

    auto space = advance(p, s, d, Action{Mode::Space, 4, 35});
    close(space.next.indoor_c, 20);
    close(space.electric_kwh, 1.0 / 3.0);
    close(space.cost_eur, 0.1);
    auto dhw = advance(p, s, d, Action{Mode::Dhw, 4, 50});
    close(dhw.next.tank_kwh, 6.5);
    close(dhw.next.indoor_c, 19.9);

    std::vector<Period> periods(8, Period{0, 0.2, 0, 0, 0});
    periods[0].total_price_eur_per_kwh = 0.05;
    periods[1].total_price_eur_per_kwh = 0.05;
    periods[2].total_price_eur_per_kwh = 0.8;
    periods[3].total_price_eur_per_kwh = 0.8;
    p.indoor_min_c = 19.5;
    p.indoor_max_c = 21;
    require(optimize(p, s, periods.data(), periods.size(), 128).status ==
            OptimizeStatus::ResourceLimit, "grid memory budget must be enforced");
    auto result = optimize(p, s, periods.data(), periods.size());
    require(result.status == OptimizeStatus::Ok, "optimizer must find feasible schedule");
    require(result.count == periods.size(), "schedule length must match horizon");
    State current = s;
    for (size_t i = 0; i < result.count; ++i) {
        auto step = advance(p, current, periods[i], result.actions[i]);
        require(feasible(p, step.next), "optimized trajectory must remain comfortable");
        current = step.next;
    }
    require(current.indoor_c >= s.indoor_c - 1e-6, "terminal room temperature must be restored");
    require(current.tank_kwh >= s.tank_kwh - 1e-6, "terminal hot water must be restored");
    require(optimize(p, s, nullptr, 0, 128).status == OptimizeStatus::InvalidInput,
            "missing forecast must fail safely");
    Parameters cold = p;
    cold.building_capacity_kwh_per_c = 8;
    cold.building_loss_kw_per_c = 0.24;
    cold.tank_loss_kw_per_kwh = 0.002;
    cold.pump_derate_kw_per_c_below_zero = 0.1;
    cold.cop_at_reference = 3.4;
    cold.cop_outdoor_per_c = 0.055;
    cold.cop_supply_per_c = 0.025;
    cold.minimum_cop = 1.2;
    cold.maximum_cop = 5;
    cold.emitter_kw_per_c = 0.8;
    cold.indoor_min_c = 19;
    cold.indoor_max_c = 21;
    cold.tank_min_kwh = 2;
    std::vector<Period> cold_periods;
    for (size_t t = 0; t < 48; ++t) {
        cold_periods.push_back({-4 + 2 * std::sin(2 * 3.141592653589793 * t / 96),
            (t < 16 ? 0.08 : 0.42) + (t % 12 < 4 ? 0.02 : 0),
            (t >= 28 ? 0.2 : 0.0),
            (t % 24 == 7 || t % 24 == 8 || t % 24 == 20) ? 2.0 : 0.0, 0});
    }
    for (size_t horizon : {size_t(24), size_t(48)}) {
        const auto cold_result = optimize(cold, State{20, 5}, cold_periods.data(), horizon);
        require(cold_result.status == OptimizeStatus::Ok,
                "cold feasible scenario must not be lost to grid rounding");
    }
    std::vector<BuildingSample> observations;
    for (int i = 0; i < 8; ++i) {
        const double indoor = 19 + (i % 3);
        const double outdoor = -5 + 2 * i;
        const double heat = (i % 4) * 2;
        const double next = indoor + step_hours *
            (heat - 0.2 * (indoor - outdoor)) / 10;
        observations.push_back({indoor, outdoor, heat, 0, next});
    }
    BuildingCalibration fitted{};
    require(fit_building(observations.data(), observations.size(), fitted),
            "calibration must fit excited measurements");
    close(fitted.capacity_kwh_per_c, 10, 1e-8);
    close(fitted.loss_kw_per_c, 0.2, 1e-8);
    require(!fit_building(observations.data(), 1, fitted),
            "calibration must reject insufficient history");
    double total_price = 0;
    Tariff tariff{};
    tariff.spot_eur_per_kwh = -0.05;
    tariff.margin_eur_per_kwh = 0.02;
    tariff.transfer_eur_per_kwh = 0.05;
    tariff.electricity_tax_eur_per_kwh = 0.02;
    tariff.vat_rate = 0.24;
    require(calculate_total_price(tariff, total_price),
            "negative spot price is a valid input");
    close(total_price, 0.0496);
    tariff.spot_includes_vat = true;
    require(calculate_total_price(tariff, total_price),
            "spot VAT status is explicit");
    close(total_price, 0.0616);
    tariff.vat_rate = -0.1;
    require(!calculate_total_price(tariff, total_price),
            "negative VAT rate must be rejected");
    periods[0].total_price_eur_per_kwh = -0.01;
    require(optimize(p, s, periods.data(), periods.size()).status == OptimizeStatus::Ok,
            "negative total prices must be optimized safely");
    std::vector<ForecastSlot> autumn_day(100);
    constexpr int64_t start_utc = 1792875600; // 2026-10-24 21:00 UTC
    for (size_t i=0;i<autumn_day.size();++i)
        autumn_day[i]={start_utc+static_cast<int64_t>(i)*900,0.1,0};
    require(inspect_forecast(autumn_day.data(),autumn_day.size(),start_utc+60,
            start_utc-3600,start_utc-3600,100)==ForecastStatus::Ready,
            "UTC forecast must allow a 100-slot Finnish autumn day");
    autumn_day[50].start_utc+=900;
    require(inspect_forecast(autumn_day.data(),autumn_day.size(),start_utc+60,
            start_utc-3600,start_utc-3600,100)==ForecastStatus::Gap,
            "missing quarter hour must disable optimization");
    autumn_day[50].start_utc-=900;
    require(inspect_forecast(autumn_day.data(),autumn_day.size(),start_utc+60,
            start_utc-172800,start_utc-3600,100)==ForecastStatus::StalePrice,
            "old prices must disable optimization");
    int64_t parsed=0;
    require(parse_utc_z("2026-10-24T21:00:00Z",parsed) && parsed==start_utc,
            "UTC source timestamps must convert exactly");
    require(parse_utc_z("2026-10-24T21:00:00.123Z",parsed) && parsed==start_utc,
            "fractional source timestamps must convert exactly");
    require(!parse_utc_z("2026-02-30T21:00:00Z",parsed) &&
            !parse_utc_z("2026-10-24T21:00:00+03:00",parsed),
            "invalid or non-UTC source timestamps must be rejected");
    std::vector<MarketPrice> source_prices(100);
    std::vector<PriceSlot> price_slots(100);
    for(size_t i=0;i<source_prices.size();++i)
        source_prices[i]={start_utc+static_cast<int64_t>(i)*900,
            start_utc+static_cast<int64_t>(i+1)*900,i==1 ? -10.0 : 0.0};
    Tariff local_tariff{};
    local_tariff.margin_eur_per_kwh=0.01;
    local_tariff.transfer_eur_per_kwh=0.05;
    local_tariff.vat_rate=0.25;
    require(normalize_nordpool_fi(source_prices.data(),source_prices.size(),
            local_tariff,price_slots.data(),price_slots.size()),
            "100 quarter-hour UTC prices must normalize across Finnish DST");
    close(price_slots[0].total_eur_per_kwh,0.075);
    close(price_slots[1].total_eur_per_kwh,0.0625);
    source_prices[50].start_utc+=900;
    require(!normalize_nordpool_fi(source_prices.data(),source_prices.size(),
            local_tariff,price_slots.data(),price_slots.size()),
            "a missing Nord Pool slot must fail closed");
    source_prices[50].start_utc-=900;
    source_prices[50].eur_per_mwh=std::numeric_limits<double>::quiet_NaN();
    require(!normalize_nordpool_fi(source_prices.data(),source_prices.size(),
            local_tariff,price_slots.data(),price_slots.size()),
            "missing or nonfinite Nord Pool price must never become zero");
    const char* fmi_xml="<wfs:FeatureCollection>"
        "<BsWfs:BsWfsElement><BsWfs:Time>2026-10-24T21:00:00Z</BsWfs:Time>"
        "<BsWfs:ParameterName>temperature</BsWfs:ParameterName>"
        "<BsWfs:ParameterValue>-3.5</BsWfs:ParameterValue></BsWfs:BsWfsElement>"
        "<BsWfs:BsWfsElement><BsWfs:Time>2026-10-24T22:00:00Z</BsWfs:Time>"
        "<BsWfs:ParameterName>temperature</BsWfs:ParameterName>"
        "<BsWfs:ParameterValue>-4</BsWfs:ParameterValue></BsWfs:BsWfsElement>"
        "</wfs:FeatureCollection>";
    OutdoorPoint outdoor[4];
    size_t outdoor_count=0;
    require(parse_fmi_temperature(fmi_xml,outdoor,4,outdoor_count) &&
            outdoor_count==2 && outdoor[0].timestamp_utc==start_utc &&
            outdoor[1].temperature_c==-4,
            "FMI hourly UTC temperatures must parse");
    double quarter_hour[5];
    require(interpolate_fmi_temperature(outdoor,outdoor_count,start_utc,
            quarter_hour,5),"hourly FMI data must interpolate to 15 minutes");
    close(quarter_hour[1],-3.625);
    close(quarter_hour[4],-4);
    require(!interpolate_fmi_temperature(outdoor,outdoor_count,start_utc-900,
            quarter_hour,5),"missing first FMI slot must not be extrapolated");
    const char* incomplete_fmi="<wfs:FeatureCollection>"
        "<BsWfs:BsWfsElement><BsWfs:Time>2026-10-24T21:00:00Z</BsWfs:Time>"
        "<BsWfs:ParameterName>temperature</BsWfs:ParameterName>"
        "<BsWfs:ParameterValue>NaN</BsWfs:ParameterValue></BsWfs:BsWfsElement>"
        "</wfs:FeatureCollection>";
    require(!parse_fmi_temperature(incomplete_fmi,outdoor,4,outdoor_count),
            "malformed FMI temperatures must fail closed");
    SafetyInputs health{};
    health.now_utc = start_utc;
    health.telemetry_utc = start_utc-10;
    health.pump_connected = true;
    health.clock_trusted = true;
    health.forecast = ForecastStatus::Ready;
    health.optimizer_elapsed_ms = 100;
    health.watchdog_ok = true;
    Supervisor shadow;
    require(shadow.review(35,health)==GateReason::Shadow,
            "reboot/default supervisor must never authorize a write");
    Supervisor lab(true);
    health.telemetry_utc=start_utc-600;
    require(lab.review(35,health)==GateReason::StaleTelemetry,
            "stale pump readings must block writes");
    health.telemetry_utc=start_utc-10;
    health.defrosting=true;
    require(lab.review(35,health)==GateReason::PumpProtection,
            "defrost must block writes");
    health.defrosting=false;
    require(lab.review(35,health)==GateReason::Allowed,
            "healthy authorized lab proposal may pass");
    require(lab.review(35,health)==GateReason::AckPending,
            "unacknowledged command must block repeats");
    lab.acknowledge(35,true);
    health.now_utc+=60;
    require(lab.review(35,health)==GateReason::RateLimited,
            "writes must have a minimum interval");
    struct Reader : TopicReader {
        double outlet=35;
        bool read_number(const char* topic, double& value) override {
            const std::string name(topic);
            if (name=="Main_Inlet_Temp") value=30;
            else if (name=="Main_Outlet_Temp") value=outlet;
            else if (name=="Outside_Temp") value=-5;
            else if (name=="DHW_Temp") value=48;
            else if (name=="Compressor_Freq") value=40;
            else if (name=="Defrosting_State") value=1;
            else if (name=="Operating_Mode_State") value=4;
            else return false;
            return true;
        }
    } reader;
    HeishaMonAdapter adapter(reader);
    PumpTelemetry reading{};
    require(adapter.poll(reading), "HeishaMon adapter must map supported readings");
    close(reading.outlet_c,35);
    close(reading.inlet_c,30);
    require(reading.defrosting, "defrost must be reported to supervisor");
    reader.outlet=250;
    require(!adapter.poll(reading), "invalid pump telemetry must be rejected");
    Parameters radiator=cold;
    radiator.building_capacity_kwh_per_c=10;
    radiator.building_loss_kw_per_c=0.21;
    radiator.pump_max_thermal_kw=7;
    radiator.pump_derate_kw_per_c_below_zero=0.07;
    radiator.cop_at_reference=3;
    radiator.cop_outdoor_per_c=0.07;
    radiator.cop_supply_per_c=0.035;
    radiator.emitter_kw_per_c=0.36;
    radiator.space_supply_high_c=50;
    radiator.dhw_supply_c=52;
    std::vector<Period> radiator_periods;
    const double prices[]={0.12,0.50,0.06,0.28};
    for(size_t t=0;t<24;++t)
        radiator_periods.push_back({-2+4*std::sin(2*3.141592653589793*t/96),
            prices[t/8],0,(t==7 || t==8 || t==20)?2.0:0.0,0});
    const auto radiator_result=optimize(radiator,State{20,5},
        radiator_periods.data(),radiator_periods.size());
    require(radiator_result.status==OptimizeStatus::Ok &&
            radiator_result.cost_eur<0.99,
            "emitter-limited case should stay near the independent MILP cost");
    Parameters invalid=p;
    invalid.pump_derate_kw_per_c_below_zero=std::numeric_limits<double>::quiet_NaN();
    require(!valid(invalid) &&
            optimize(invalid,s,periods.data(),periods.size()).status==OptimizeStatus::InvalidInput,
            "non-finite pump calibration must never enter optimization");
    std::cout << "core tests passed\n";
}
