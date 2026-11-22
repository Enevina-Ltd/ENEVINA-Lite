#ifndef COTTAGE_BENCH_MODE
#include "cottage_panasonic_service.hpp"
#include "cottage_serial_heating_service.hpp"
#include "pump/panasonic_uart.hpp"
#include <esp_system.h>

namespace {
using namespace enevina;
using namespace enevina::cottage;
namespace wire=enevina::panasonic;

struct PanasonicWire : SerialHeatingWire {
    using Settings=wire::Climate;
    using Patch=wire::Patch;
    static constexpr auto owner=PumpUartOwner::Panasonic;
    static constexpr uint32_t format=SERIAL_8E1;
    static constexpr const char* adapter_id="panasonic_uart";
    static constexpr const char* api_root="/api/panasonic";
    static constexpr const char* nvs="cottage_pan";
    static constexpr bool legacy_baud=false;
    static constexpr double min_target=16,max_target=30;
    static bool automatic_enabled() {return cottage_panasonic_automatic_enabled();}
    static void automatic_cancel() {cottage_panasonic_automatic_cancel();}
    static bool automatic_write_current(uint32_t authorization,uint32_t config,uint32_t settings,
                                        const BootstrapDecision& proposal) {
        return cottage_panasonic_automatic_write_current(authorization,config,settings,proposal);
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
    inline static Settings initial{};
    inline static std::array<uint8_t,6> module_mac{};
    inline static wire::Reader reader{};
    inline static std::atomic<uint8_t> tx{0};
    inline static std::optional<bool> defrost{};
    inline static uint32_t defrost_ms=0,last_send=0;
    inline static bool initial_received=false;
    inline static wire::Family family=wire::Family::Cnt;

    static auto protocol(const Settings& state) {
        return state.family==wire::Family::Cnt?PumpRoomProtocol::PanasonicCnt:PumpRoomProtocol::PanasonicWlan;
    }
    static const char* protocol_label(const Settings& state) {
        return state.family==wire::Family::Cnt?"CN-CNT":"CN-WLAN";
    }
    static double step(const Settings& state) {return state.defrost==true || !state.target_c?0:.5;}
    static bool same_settings(const Settings& previous,const Settings& current) {
        return wire::same_settings(previous,current);
    }
    static auto command(const Settings& before,const Patch& patch,const PumpHeatingLimits& limits) {
        return wire::command(before,patch,limits,tx.load());
    }
    template<typename Link>
    static bool transmit(const wire::Packet& p,const Link& link) {
        if(!p.size || !link.current() || !cottage_pump_uart.send(p.bytes.data(),p.size))return false;
        last_send=millis();
        return true;
    }
    template<typename Link>
    static void side_packet(const wire::Packet& packet,const Link& link) {
        if(family!=wire::Family::Wlan || !link.current())return;
        if(const auto status=wire::defrost_status(packet)) {
            defrost=status;defrost_ms=millis();
        }
        if(packet.is_wlan(1,1) || packet.is_wlan(0x10,0x0a))
            transmit(wire::wlan_reply(packet),link);
    }
    template<typename Link,typename Match>
    static std::optional<wire::Packet> receive(const Link& link,uint32_t window_ms,Match matches) {
        const uint32_t started=millis();
        auto& uart=cottage_pump_uart.port();
        do {
            for(unsigned budget=0;budget<240 && link.current() && uart.available();++budget) {
                const int next=uart.read();
                if(next<0)break;
                if(auto packet=reader.append(static_cast<uint8_t>(next))) {
                    if(matches(*packet))return packet;
                    side_packet(*packet,link);
                }
            }
            if(window_ms)vTaskDelay(pdMS_TO_TICKS(12));
        } while(link.current() && millis()-started<window_ms);
        return {};
    }
    template<typename Link>
    static void idle(const Link& link) {
        if(family==wire::Family::Wlan)
            receive(link,0,[](const auto&){return false;});
    }
    template<typename Link>
    static bool prepare_to_write(const Link& link) {
        if(family==wire::Family::Cnt) {
            const uint32_t elapsed=millis()-last_send;
            if(elapsed<250)vTaskDelay(pdMS_TO_TICKS(250-elapsed));
            return cottage_pump_uart.drain();
        }
        idle(link);
        return link.current() && !(defrost==true && millis()-defrost_ms<90000);
    }
    static void write_dispatched(const Settings& state) {
        last_send=millis();
        if(state.family==wire::Family::Wlan)tx.store(wire::next_sequence(tx.load()));
    }
    template<typename Link>
    static std::optional<Settings> cnt_query(const Link& link) {
        reader={};
        if(!cottage_pump_uart.drain() || !transmit(wire::cnt_poll(),link))return {};
        const auto answer=receive(link,850,[](const auto& p){return wire::climate(p,wire::Family::Cnt).has_value();});
        return answer?wire::climate(*answer,wire::Family::Cnt):std::nullopt;
    }
    template<typename Link>
    static std::optional<Settings> wlan_query(const Link& link) {
        idle(link);
        const uint8_t seq=tx.load();
        if(!transmit(wire::wlan_poll(seq),link))return {};
        tx.store(wire::next_sequence(seq));
        const auto answer=receive(link,1200,[seq](const auto& p){return wire::climate(p,wire::Family::Wlan,seq).has_value();});
        if(!answer)return {};
        auto reading=wire::climate(*answer,wire::Family::Wlan,seq);
        if(defrost && millis()-defrost_ms<90000)reading->defrost=defrost;
        return reading;
    }
    template<typename Link>
    static std::optional<bool> handshake(const Link& link) {
        if(esp_read_mac(module_mac.data(),ESP_MAC_WIFI_STA)!=ESP_OK)return false;
        tx.store(0);
        if(!transmit(wire::wlan_handshake(1,0).packet,link))return false;
        tx.store(1);
        vTaskDelay(pdMS_TO_TICKS(12));
        for(unsigned i=2;i<=12 && link.current();++i) {
            const uint8_t seq=tx.load();
            const auto step=wire::wlan_handshake(i,seq);
            bool answered=false;
            const unsigned attempts=i==2?3:1;
            for(unsigned a=0;a<attempts && !answered;++a) {
                if(!transmit(step.packet,link))return false;
                answered=receive(link,720,[&](const auto& p){
                    return p.is_wlan(step.reply_group,step.reply_code);
                }).has_value();
            }
            if(!answered) {
                if(i==2 && link.current())return std::nullopt;
                return false;
            }
            tx.store(wire::next_sequence(seq));
        }
        const auto initiate=wire::wlan_handshake(13,tx.load());
        if(!transmit(initiate.packet,link))return false;
        tx.store(wire::next_sequence(tx.load()));
        const auto hello=receive(link,1800,[](const auto& p){return p.is_wlan(1,9);});
        if(!hello || !transmit(wire::wlan_reply(*hello,module_mac),link))return false;
        const auto versions=receive(link,1800,[](const auto& p){return p.is_wlan(0,0x20);});
        if(!versions || !transmit(wire::wlan_reply(*versions),link))return false;
        vTaskDelay(pdMS_TO_TICKS(650));
        if(!wlan_query(link))return false;
        const uint32_t first_poll_complete=millis();
        while(link.current() && millis()-first_poll_complete<10000) {
            idle(link);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        const auto last=wire::wlan_handshake(16,tx.load());
        if(!transmit(last.packet,link))return false;
        tx.store(wire::next_sequence(tx.load()));
        return receive(link,1200,[&](const auto& p){
            return p.is_wlan(last.reply_group,last.reply_code);
        }).has_value();
    }
    template<typename Link>
    static bool connect(const Link& link) {
        // WLAN units may also emit unsolicited 0x70 defrost frames. Once the
        // WLAN probe responds, do not reinterpret those as a CNT connection.
        family=wire::Family::Wlan;defrost={};reader={};initial_received=false;
        if(!cottage_pump_uart.drain())return false;
        if(const auto wlan=handshake(link))return *wlan;

        family=wire::Family::Cnt;
        if(!cnt_query(link))return false;
        vTaskDelay(pdMS_TO_TICKS(250));
        const auto state=cnt_query(link);
        if(!state)return false;
        initial=*state;initial_received=true;
        return true;
    }
    template<typename Sample,typename Link>
    static bool fill(Sample& sample,const Link& link) {
        const auto reading=initial_received?std::optional<Settings>{initial}:
            family==wire::Family::Cnt?cnt_query(link):wlan_query(link);
        initial_received=false;
        if(!reading)return false;
        sample.climate=*reading;
        sample.received_utc=time(nullptr);
        sample.temperature.indoor_c=reading->indoor_c;
        sample.temperature.outdoor_c=reading->outdoor_c;
        return true;
    }
};
using Service=SerialHeatingService<PanasonicWire>;
}
bool cottage_panasonic_selected() {return Service::selected();}
bool cottage_panasonic_needs_worker() {return Service::needs_worker();}
void cottage_panasonic_register_routes(WebServer& web) {Service::register_routes(web);}
void cottage_panasonic_tick() {Service::tick();}
uint32_t cottage_panasonic_configuration_generation() {return Service::configuration_generation();}
uint32_t cottage_panasonic_calibration_identity() {return Service::calibration_identity();}
uint32_t cottage_panasonic_bus_generation() {return Service::bus_generation();}
bool cottage_panasonic_automatic_ready() {return Service::automatic_ready();}
enevina::PumpRoomState cottage_panasonic_room_state() {return Service::room_state();}
enevina::cottage::ControlBlock cottage_panasonic_submit_automatic(
        const enevina::PumpRoomState& pump,enevina::cottage::ControlSnapshot view,
        uint32_t authorization,uint32_t config,uint32_t settings) {
    return Service::submit_automatic(pump,view,authorization,config,settings);
}
#endif
