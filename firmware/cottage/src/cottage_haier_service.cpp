#ifndef COTTAGE_BENCH_MODE
#include "cottage_haier_service.hpp"
#include "cottage_serial_heating_service.hpp"
#include "pump/haier_uart.hpp"

namespace {
using namespace enevina;
using namespace enevina::cottage;
namespace wire=enevina::haier;

struct HaierWire : SerialHeatingWire {
    using Settings=wire::Climate;
    using Patch=wire::Patch;
    static constexpr auto owner=PumpUartOwner::Haier;
    static constexpr uint32_t format=SERIAL_8N1;
    static constexpr const char* adapter_id="haier_uart";
    static constexpr const char* api_root="/api/haier";
    static constexpr const char* nvs="cottage_haier";
    static constexpr bool legacy_baud=false;
    static constexpr double min_target=16,max_target=30;
    inline static wire::Family family=wire::Family::Hon;
    inline static bool crc=false;

    static auto protocol(const Settings& state) {
        return state.family==wire::Family::Hon?PumpRoomProtocol::HaierHon:PumpRoomProtocol::HaierSmartAir2;
    }
    static const char* protocol_label(const Settings& state) {
        return state.family==wire::Family::Hon?"hOn":"SmartAir2";
    }
    static bool automatic_enabled() {return cottage_haier_automatic_enabled();}
    static void automatic_cancel() {cottage_haier_automatic_cancel();}
    static bool automatic_write_current(uint32_t authorization,uint32_t config,uint32_t settings,
                                        const BootstrapDecision& proposal) {
        return cottage_haier_automatic_write_current(authorization,config,settings,proposal);
    }
    static std::optional<bool> power(const Settings& state) {return state.power;}
    static std::optional<unsigned> mode(const Settings& state) {
        if(state.mode==wire::Mode::Unknown)return {};
        return static_cast<unsigned>(state.mode);
    }
    static bool is_heat(const Settings& state) {return state.mode==wire::Mode::Heat;}
    static PumpRoomMode heating_mode(const Settings& state) {
        if(!state.power)return PumpRoomMode::Off;
        return is_heat(state)?PumpRoomMode::Heat:PumpRoomMode::Other;
    }
    static bool readback_matches(const Settings& before,const Settings& after,const Patch& patch) {
        return wire::readback_matches(before,after,patch);
    }
    static bool valid_baud(uint32_t baud) {return baud==9600;}
    static uint32_t requested_baud(WebServer&) {return 9600;}
    static double step(const Settings& state) {
        return state.cooling_only || state.fahrenheit || state.protected_mode?0:1;
    }
    static bool same_settings(const Settings& a,const Settings& b) {return wire::same_settings(a,b);}
    static auto command(const Settings& before,const Patch& patch,const PumpHeatingLimits& limits) {
        return wire::command(before,patch,limits);
    }
    template<typename Link,typename Match>
    static std::optional<wire::Frame> exchange(const wire::Packet& request,const Link& link,Match matches) {
        if(!link.current() || !cottage_pump_uart.drain() ||
           !cottage_pump_uart.send(request.bytes.data(),request.size))return {};
        auto& uart=cottage_pump_uart.port();
        wire::Reader reader;
        const uint32_t begin=millis();
        while(link.current() && millis()-begin<1100) {
            for(unsigned i=0;i<64 && uart.available();++i) {
                const int next=uart.read();
                if(next<0)break;
                if(auto frame=reader.append(static_cast<uint8_t>(next));frame && matches(*frame))
                    return frame;
            }
            vTaskDelay(pdMS_TO_TICKS(12));
        }
        return {};
    }
    template<typename Link>
    static bool connect(const Link& link) {
        const auto version=exchange(wire::version_query(),link,[](const auto& f){
            return f.type==wire::Type::VersionReply || f.type==wire::Type::Invalid;
        });
        if(!version)return false;
        if(version->type==wire::Type::Invalid && version->size==0 && !version->crc) {
            family=wire::Family::SmartAir2;crc=false;
            return true;
        }
        const auto negotiated=wire::hon_crc(*version);
        if(!negotiated)return false;
        family=wire::Family::Hon;crc=*negotiated;
        vTaskDelay(pdMS_TO_TICKS(100));
        return exchange(wire::query(wire::Type::DeviceId,crc),link,[](const auto& f){
            return f.type==wire::Type::DeviceIdReply && f.crc==crc && f.size>=32;
        }).has_value();
    }
    template<typename Sample,typename Link>
    static bool fill(Sample& sample,const Link& link) {
        const auto poll=wire::query(wire::Type::Control,crc,wire::read_status);
        const auto answer=exchange(poll,link,[](const auto& f){
            return f.type==wire::Type::Status && f.subtype()==wire::status_reply && f.crc==crc;
        });
        const auto state=answer?wire::climate(*answer,family,crc):std::nullopt;
        if(!state)return false;
        sample.climate=*state;
        sample.received_utc=time(nullptr);
        sample.temperature.indoor_c=state->indoor_c;
        sample.temperature.outdoor_c=state->outdoor_c;
        return true;
    }
};
using Service=SerialHeatingService<HaierWire>;
}
bool cottage_haier_selected() {return Service::selected();}
bool cottage_haier_needs_worker() {return Service::needs_worker();}
void cottage_haier_register_routes(WebServer& web) {Service::register_routes(web);}
void cottage_haier_tick() {Service::tick();}
uint32_t cottage_haier_configuration_generation() {return Service::configuration_generation();}
uint32_t cottage_haier_calibration_identity() {return Service::calibration_identity();}
uint32_t cottage_haier_bus_generation() {return Service::bus_generation();}
bool cottage_haier_automatic_ready() {return Service::automatic_ready();}
enevina::PumpRoomState cottage_haier_room_state() {return Service::room_state();}
enevina::cottage::ControlBlock cottage_haier_submit_automatic(
        const enevina::PumpRoomState& pump,enevina::cottage::ControlSnapshot view,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    return Service::submit_automatic(pump,view,authorization,config,settings);
}
#endif
