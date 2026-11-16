#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "cottage/ruuvi_v5.hpp"
namespace enevina::cottage {

struct AirMoisture {
    bool valid=false;
    double temperature_c=0;
    double rh_pct=0;
    double dew_point_c=0;
    double absolute_humidity_gm3=0;
    double vapour_pressure_pa=0;
};
AirMoisture calculate_air_moisture(double temperature_c,double rh_pct);
struct VentilationEstimate {
    bool available=false;
    double inside_outside_c=0;
    double inside_minus_outside_gm3=0;
    // A positive difference suggests drying potential, NOT an instruction
    // to ventilate a particular structure or a wall-condensation prediction.
    bool possible_drying=false;
};
VentilationEstimate estimate_ventilation(const AirMoisture& indoor,
                                        const AirMoisture& outdoor);

enum class MoistureAlertKind : uint8_t { HighRh70=0, HighRh80=1, SensorMissing=2, LowBattery=3, Freezing=4 };
constexpr size_t moisture_alert_count=5;
struct MoistureAlert {
    bool active=false,acknowledged=false;
    uint64_t since_ms=0;
};
struct AdvisoryLimits {
    double monitor_rh=70.0,review_rh=80.0;
    double clear_hysteresis_rh=5.0;
    uint64_t monitor_after_ms=6ULL*3600000,review_after_ms=24ULL*3600000;
    uint64_t sensor_timeout_ms=600000;
    double low_battery_v=2.50;
};
struct AirHistoryPoint {
    uint32_t hour_utc=0;
    int16_t temperature_centi=0;
    uint16_t rh_centi=0;
    int16_t dewpoint_centi=0;
    uint16_t absolute_humidity_centi=0;
};
constexpr size_t cottage_history_hours=168;
struct AirHistoryDump {
    uint32_t magic=0x434F5448; // COTH
    uint16_t version=1;
    uint16_t count=0;
    std::array<AirHistoryPoint,cottage_history_hours> values{};
    uint32_t checksum=0;
};
class AirHistory {
public:
    bool add(const AirMoisture& air,int64_t unix_utc);
    // Without NTP, continue RAM history with an explicitly untrusted
    // monotonic-hour marker; never persist it as civil UTC.
    bool add_offline(const AirMoisture& air,uint64_t monotonic_ms);
    size_t count()const {return count_;}
    const AirHistoryPoint& newest(size_t newest_offset)const;
    AirHistoryDump dump()const;
    bool restore(const AirHistoryDump& saved);
private:
    std::array<AirHistoryPoint,cottage_history_hours> points_{};
    uint16_t count_=0;
    uint16_t next_=0;
    bool add_hour(const AirMoisture& air,uint32_t hour);
};
class MoistureMonitor {
public:
    explicit MoistureMonitor(AdvisoryLimits limits={}):limits_(limits){}
    const AdvisoryLimits& limits()const {return limits_;}
    void update_limits(const AdvisoryLimits& limits) {
        // Changing warning definitions cannot inherit old exposure time
        // from different thresholds without re-evaluation.
        *this=MoistureMonitor(limits);
    }
    void observe(const RuuviV5& reading,uint64_t monotonic_ms);
    void tick(uint64_t monotonic_ms,bool sensor_bound);
    const MoistureAlert& alert(MoistureAlertKind kind)const {
        return alerts_[static_cast<size_t>(kind)];
    }
    void acknowledge(MoistureAlertKind kind) {
        auto& a=alerts_[static_cast<size_t>(kind)];
        if(a.active)a.acknowledged=true;
    }
    uint64_t high70_duration_ms(uint64_t now)const;
    uint64_t high80_duration_ms(uint64_t now)const;
    bool recent(uint64_t now)const {
        return has_measurement_ && now>=last_ms_ &&
               now-last_ms_<=limits_.sensor_timeout_ms;
    }
private:
    AdvisoryLimits limits_{};
    std::array<MoistureAlert,moisture_alert_count> alerts_{};
    uint64_t last_ms_=0,above70_since_=0,above80_since_=0;
    uint64_t bound_since_ms_=0;
    bool bound_seen_=false;
    bool has_measurement_=false,above70_=false,above80_=false;
    void assign(MoistureAlertKind kind,bool active,uint64_t now);
};

enum class CottageMode : uint8_t { Cold=0,Drying=1,Hold=2 };
struct CottageModeConfig {
    CottageMode mode=CottageMode::Cold;
    double hold_target_c=12.0;
    double drying_delta_c=4.0;
};
enum class ModeAdvisory : uint8_t {
    NoHeating, SensorUnavailable, MonitoringHumidity, DryingSuggested,
    BelowTemperatureTarget, UnsupportedBelow16, NoOutdoorTemperature
};
struct CottageModePlan {
    CottageMode mode=CottageMode::Cold;
    bool has_target=false,advisory_heat_needed=false;
    bool direct_setpoint_below_mitsubishi_min=false;
    bool physical_writes_allowed=false; // C2.3 NEVER writes to a pump.
    double target_c=0;
    ModeAdvisory advisory=ModeAdvisory::NoHeating;
};
CottageModePlan evaluate_cottage_mode(const CottageModeConfig& config,
      const AirMoisture& indoor,bool indoor_fresh,
      bool have_outdoor_temperature,double outdoor_c,
      uint64_t high70_duration_ms);
}
