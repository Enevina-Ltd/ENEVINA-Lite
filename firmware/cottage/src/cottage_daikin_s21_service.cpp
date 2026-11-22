#ifndef COTTAGE_BENCH_MODE
#include "cottage_daikin_s21_service.hpp"
#include "cottage_serial_heating_service.hpp"
#include "pump/daikin_s21.hpp"

namespace {
using namespace enevina;
using namespace enevina::cottage;
namespace wire=enevina::s21;

struct S21Wire {
    using Settings=wire::Climate;
    using Patch=wire::Patch;
    static constexpr auto owner=PumpUartOwner::DaikinS21;
    static auto protocol(const Settings&) {return PumpRoomProtocol::DaikinS21;}
    static const char* protocol_label(const Settings&) {return "S21";}
    static constexpr uint32_t format=SERIAL_8E2;
    static constexpr const char* adapter_id="daikin_s21";
    static constexpr const char* api_root="/api/daikin-s21";
    static constexpr const char* nvs="cottage_s21";
    static constexpr bool legacy_baud=false;
    static constexpr double min_target=10,max_target=35;

    static bool automatic_enabled() {return cottage_daikin_s21_automatic_enabled();}
    static void automatic_cancel() {cottage_daikin_s21_automatic_cancel();}
    static bool automatic_write_current(uint32_t authorization,uint32_t config,uint32_t settings,
                                        const BootstrapDecision& proposal) {
        return cottage_daikin_s21_automatic_write_current(authorization,config,settings,proposal);
    }
    static std::optional<bool> power(const Settings& state) {return state.power;}
    static std::optional<unsigned> mode(const Settings& state) {
        if(state.mode==wire::Mode::Unknown)return {};
        return static_cast<unsigned>(state.mode);
    }
    static bool is_heat(const Settings& state) {return state.mode==wire::Mode::Heat;}
    static PumpRoomMode heating_mode(const Settings& state) {return wire::heating_mode(state);}
    static bool readback_matches(const Settings& before,const Settings& after,const Patch& patch) {
        return wire::readback_matches(before,after,patch);
    }

    static bool valid_baud(uint32_t baud) {return baud==2400;}
    static uint32_t requested_baud(WebServer&) {return 2400;}
    static double step(const Settings&) {return .5;}
    static bool same_settings(const Settings& a,const Settings& b) {return a.wire==b.wire;}
    static auto command(const Settings& before,const Patch& patch,const PumpHeatingLimits& limits) {
        return wire::command(before,patch,limits);
    }
    template<typename Link>
    static bool connect(const Link&) {return true;}
    template<typename Link>
    static std::optional<wire::Frame> exchange(wire::Code outgoing,wire::Code incoming,const Link& link) {
        const auto request=wire::query(outgoing);
        if(!link.current() || !cottage_pump_uart.drain() ||
           !cottage_pump_uart.send(request.bytes.data(),request.size))return {};
        auto& uart=cottage_pump_uart.port();
        wire::Reader reader;
        bool inside_frame=false;
        const uint32_t begin=millis();
        while(link.current() && millis()-begin<1100) {
            for(unsigned i=0;i<40 && uart.available();++i) {
                const int next=uart.read();
                if(next<0)break;
                if(!inside_frame && next==wire::nak)return {};
                if(next==wire::stx)inside_frame=true;
                if(next==wire::etx)inside_frame=false;
                if(const auto verified=reader.append(static_cast<uint8_t>(next))) {
                    if(wire::response(*verified,incoming,4)) {
                        constexpr uint8_t ack=wire::ack;
                        cottage_pump_uart.send(&ack,1);
                        return verified;
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(12));
        }
        return {};
    }
    template<typename Sample,typename Link>
    static bool fill(Sample& sample,const Link& link) {
        const auto answer=exchange(wire::read_climate,wire::climate_reply,link);
        const auto climate=answer?wire::climate(*answer):std::nullopt;
        if(!climate || climate->mode==wire::Mode::Unknown)return false;
        sample.climate=*climate;
        sample.received_utc=time(nullptr);
        if(const auto reply=exchange(wire::read_room,wire::room_reply,link))
            sample.temperature.indoor_c=wire::temperature(*reply,wire::room_reply);
        if(const auto reply=exchange(wire::read_outdoor,wire::outdoor_reply,link))
            sample.temperature.outdoor_c=wire::temperature(*reply,wire::outdoor_reply);
        return true;
    }
};
using Service=SerialHeatingService<S21Wire>;
}
bool cottage_daikin_s21_selected() {return Service::selected();}
bool cottage_daikin_s21_needs_worker() {return Service::needs_worker();}
void cottage_daikin_s21_register_routes(WebServer& web) {Service::register_routes(web);}
void cottage_daikin_s21_tick() {Service::tick();}
uint32_t cottage_daikin_s21_configuration_generation() {return Service::configuration_generation();}
uint32_t cottage_daikin_s21_calibration_identity() {return Service::calibration_identity();}
uint32_t cottage_daikin_s21_bus_generation() {return Service::bus_generation();}
bool cottage_daikin_s21_automatic_ready() {return Service::automatic_ready();}
enevina::PumpRoomState cottage_daikin_s21_room_state() {return Service::room_state();}
enevina::cottage::ControlBlock cottage_daikin_s21_submit_automatic(
        const enevina::PumpRoomState& pump,enevina::cottage::ControlSnapshot view,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    return Service::submit_automatic(pump,view,authorization,config,settings);
}
#endif
