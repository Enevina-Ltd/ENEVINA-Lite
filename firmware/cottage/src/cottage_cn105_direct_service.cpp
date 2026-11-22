#ifndef COTTAGE_BENCH_MODE
#include "cottage_cn105_direct_service.hpp"
#include "cottage_serial_heating_service.hpp"
#include "pump/cn105_uart.hpp"

namespace {
using namespace enevina;
using namespace enevina::cottage;
namespace wire=enevina::cn105_uart;

struct Cn105Wire {
    using Settings=wire::Settings;
    using Patch=wire::Patch;
    static constexpr auto owner=PumpUartOwner::MitsubishiCn105;
    static constexpr auto protocol=PumpRoomProtocol::Cn105Direct;
    static constexpr uint32_t format=SERIAL_8E1;
    static constexpr const char* adapter_id="mitsubishi_cn105_direct";
    static constexpr const char* api_root="/api/cn105-direct";
    static constexpr const char* nvs="cottage_cn1";
    static constexpr bool legacy_baud=true;
    static constexpr double min_target=16,max_target=31;

    static bool automatic_enabled() {return cottage_cn105_direct_automatic_enabled();}
    static void automatic_cancel() {cottage_cn105_direct_automatic_cancel();}
    static bool automatic_write_current(uint32_t authorization,uint32_t config,uint32_t settings,
                                        const BootstrapDecision& proposal) {
        return cottage_cn105_direct_automatic_write_current(authorization,config,settings,proposal);
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

    static bool valid_baud(uint32_t baud) {return baud==2400 || baud==9600;}
    static uint32_t requested_baud(WebServer& web) {
        const String value=web.arg("baud");
        return value=="2400"?2400:value=="9600"?9600:0;
    }
    static double step(const Settings& state) {
        return state.encoding==wire::TemperatureEncoding::HalfDegree?.5:
            state.encoding==wire::TemperatureEncoding::WholeDegree?1.:0.;
    }
    static bool same_settings(const Settings& a,const Settings& b) {
        return a.power==b.power && a.mode==b.mode && a.target_c==b.target_c && a.encoding==b.encoding;
    }
    static auto command(const Settings& before,const Patch& patch,const PumpHeatingLimits& limits) {
        return wire::heat_command(before,patch,limits);
    }
    template<typename Link,typename Accept>
    static std::optional<wire::Frame> exchange(const wire::Frame& request,const Link& link,Accept accept) {
        if(!link.current() || !cottage_pump_uart.drain() ||
           !cottage_pump_uart.send(request.bytes.data(),request.size))return {};
        auto& uart=cottage_pump_uart.port();
        wire::FrameReader reader;
        const uint32_t start=millis();
        while(link.current() && millis()-start<1100) {
            for(unsigned i=0;i<40 && uart.available();++i) {
                const int next=uart.read();
                if(next<0)break;
                if(const auto reply=reader.append(static_cast<uint8_t>(next)))
                    if(accept(*reply))return reply;
            }
            vTaskDelay(pdMS_TO_TICKS(12));
        }
        return {};
    }
    template<typename Link>
    static bool connect(const Link& link) {
        return exchange(wire::connect(),link,[](const wire::Frame& reply){
            return wire::response(reply,wire::command_connect_reply);
        }).has_value();
    }
    template<typename Link,typename Decode>
    static auto read(wire::Query tag,const Link& link,Decode decode) {
        const auto incoming=exchange(wire::query(tag),link,[&](const wire::Frame& reply){
            return wire::response(reply,wire::command_data_reply) && reply.bytes[4]==wire::max_payload &&
                reply.bytes[5]==static_cast<uint8_t>(tag);
        });
        return incoming?decode(*incoming):decltype(decode(*incoming)){};
    }
    template<typename Sample,typename Link>
    static bool fill(Sample& sample,const Link& link) {
        const auto climate=read(wire::Query::Settings,link,wire::settings);
        if(!climate || !climate->power || climate->mode==wire::Mode::Unknown)return false;
        sample.climate=*climate;
        sample.received_utc=time(nullptr);
        if(const auto room=read(wire::Query::Room,link,wire::room)) {
            sample.temperature.indoor_c=room->indoor_c;
            sample.temperature.outdoor_c=room->outdoor_c;
        }
        if(const auto compressor=read(wire::Query::Operating,link,wire::operation))
            sample.compressor_hz=compressor->compressor_hz;
        return true;
    }
};
using Service=SerialHeatingService<Cn105Wire>;
}
bool cottage_cn105_direct_selected() {return Service::selected();}
bool cottage_cn105_direct_needs_worker() {return Service::needs_worker();}
void cottage_cn105_direct_register_routes(WebServer& web) {Service::register_routes(web);}
void cottage_cn105_direct_tick() {Service::tick();}
uint32_t cottage_cn105_direct_configuration_generation() {return Service::configuration_generation();}
uint32_t cottage_cn105_direct_calibration_identity() {return Service::calibration_identity();}
uint32_t cottage_cn105_direct_bus_generation() {return Service::bus_generation();}
bool cottage_cn105_direct_automatic_ready() {return Service::automatic_ready();}
enevina::PumpRoomState cottage_cn105_direct_room_state() {return Service::room_state();}
enevina::cottage::ControlBlock cottage_cn105_direct_submit_automatic(
        const enevina::PumpRoomState& pump,enevina::cottage::ControlSnapshot view,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    return Service::submit_automatic(pump,view,authorization,config,settings);
}
#endif
