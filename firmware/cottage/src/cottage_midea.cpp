#ifndef COTTAGE_BENCH_MODE
#include "cottage_midea.hpp"
#include "cottage_midea_service.hpp"
#include "cottage_wifi.hpp"
#include "cottage_site_settings.hpp"
#include "pump/midea_ac_protocol.hpp"
#include "pump/midea_discovery_info.hpp"
#include "pump/midea_read_plan.hpp"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>

namespace enevina::midea {
namespace {
constexpr uint16_t default_port=6444;
constexpr uint32_t read_timeout_ms=2000;
constexpr uint32_t max_read_ms=9500;
constexpr uint32_t max_prewrite_ms=12000;
std::atomic<uint8_t> last_v3_read_detail_code{0};
std::atomic<uint8_t> last_v3_reply_opcode_code{0};

bool allowed_lan_address(const IPAddress& ip) {
    const uint8_t a=ip[0],b=ip[1];
    return a==10 || (a==172 && b>=16 && b<=31) || (a==192 && b==168);
}
struct Budget {
    uint32_t started=static_cast<uint32_t>(millis()),duration=read_timeout_ms;
    uint32_t left()const{return exchange_time_left(started,duration,static_cast<uint32_t>(millis()));}
};
// Header and body share ONE deadline. A partial packet taints the stream;
// a silent optional query merely times out and never becomes "unsupported".
bool read_exact(WiFiClient& tcp,uint8_t* data,size_t n,const Budget& budget,bool& partial) {
    if(!data || n>1024)return false;
    size_t got=0;
    while(got<n && budget.left()) {
        const int available=tcp.available();
        if(available>0) {
            const int count=tcp.read(data+got,std::min<size_t>(n-got,available));
            if(count<1)break;
            got+=static_cast<size_t>(count);
        } else {
            if(!tcp.connected())break;
            delay(2);
        }
    }
    if(got!=n && got)partial=true;
    return got==n;
}
bool read_packet(WiFiClient& tcp,bool v3,Bytes& packet,const Budget& budget,bool& broken) {
    uint8_t header[6]{};
    if(!read_exact(tcp,header,sizeof(header),budget,broken))return false;
    size_t total=0;
    if(!(v3?packet_size(header,6,total):v2_packet_size(header,6,total))) {
        broken=true;return false;
    }
    packet.assign(total,0);
    std::copy(header,header+6,packet.begin());
    if(!read_exact(tcp,packet.data()+6,total-6,budget,broken)) {
        broken=true;return false;
    }
    return true;
}
bool send_bytes(WiFiClient& tcp,const Bytes& data) {
    return !data.empty() && tcp.write(data.data(),data.size())==data.size();
}
bool now_stamp(uint8_t out[8]) {
    const time_t t=time(nullptr);
    if(t<1700000000)return false;
    struct tm utc{};
#ifdef _WIN32
    if(gmtime_s(&utc,&t)!=0)return false;
#else
    if(!gmtime_r(&t,&utc))return false;
#endif
    out[0]=0;out[1]=static_cast<uint8_t>(utc.tm_sec);
    out[2]=static_cast<uint8_t>(utc.tm_min);out[3]=static_cast<uint8_t>(utc.tm_hour);
    out[4]=static_cast<uint8_t>(utc.tm_mday);out[5]=static_cast<uint8_t>(utc.tm_mon+1);
    out[6]=static_cast<uint8_t>((utc.tm_year+1900)%100);
    out[7]=static_cast<uint8_t>((utc.tm_year+1900)/100);
    return true;
}
bool matches_query(const Bytes& request,const Bytes& reply,const AcProfile& profile) {
    if(request.size()<11 || reply.size()<11)return false;
    // An appliance/version reply has its own message type and body layout.
    if(request[9]==0xa0)return reply[9]==0xa0;
    if(request[9]!=3)return false;
    if(request[10]==0xa0)
        return reply[10]==0xa0 && (reply[9]==3 || reply[9]==5);
    // This exact upstream model reports its requested 0x7e temperatures in a
    // B5 notify2 body. It remains auxiliary data, never capability/basic-state
    // evidence; other notifications and every SET echo stay excluded.
    if(request[10]==0xb1 && reply[9]==5 && reply[10]==0xb5 &&
       profile.model_known && profile.new_temperature_layout &&
       std::strcmp(profile.model.data(),"22013279")==0 && request.size()>12) {
        const size_t count=request[11];
        if(count>(request.size()-14)/2)return false;
        for(size_t i=0;i<count;++i)
            if(request[12+i*2]==0x7e && request[13+i*2]==0)return true;
    }
    if(reply[9]!=3)return false; // SET echoes / notifications are not readback.
    switch(request[10]) {
    case 0x41:return reply[10]==0xc0;
    case 0xb5:return reply[10]==0xb5;
    case 0xb1:return reply[10]==0xb1;
    case 0xaa:return reply.size()>15 && request.size()>15 &&
                     reply[10]==0xbb && reply[15]==request[15];
    default:return false;
    }
}
ReadIdentity identity(const IPAddress& ip,uint64_t id,bool v3,uint32_t configuration_generation) {
    return {id,static_cast<uint32_t>(ip),cottage_wifi_link_generation(),
            site_settings_endpoint_generation(),configuration_generation,static_cast<uint8_t>(v3?3:2)};
}
bool identity_current(const ReadIdentity& id) {
    return WiFi.status()==WL_CONNECTED &&
        id.link_generation==cottage_wifi_link_generation() &&
        id.endpoint_generation==site_settings_endpoint_generation() &&
        cottage_midea_configuration_current(id.configuration_generation);
}
// Used only by the single Midea worker under its existing network lease.
// Never cache live state, credentials or a claim that control was verified.
struct ProfileCache {
    ReadPlan plan{};
    AcProfile profile{};
    uint16_t port=default_port;
    uint8_t discovery_attempts=0,appliance_type=0;
};
ProfileCache cache;
void bind_cache(const ReadIdentity& id) {
    if(cache.plan.bind(id)) {
        cache.profile={};cache.port=default_port;
        cache.discovery_attempts=0;cache.appliance_type=0;
    }
}
void discover_identity(const IPAddress& target,const ReadIdentity& id) {
    if(cache.discovery_attempts>=2)return;
    ++cache.discovery_attempts;
    WiFiUDP udp;
    if(!udp.begin(0))return;
    for(const uint16_t port:{6445u,20086u}) {
        if(udp.beginPacket(target,port)) {
            udp.write(discovery_request.data(),discovery_request.size());udp.endPacket();
        }
    }
    Budget budget{static_cast<uint32_t>(millis()),500};
    std::array<uint8_t,1024> packet{};
    while(budget.left() && identity_current(id)) {
        const int n=udp.parsePacket();
        if(n<1){delay(5);continue;}
        if(udp.remoteIP()!=target || n>static_cast<int>(packet.size())) {udp.flush();continue;}
        const int got=udp.read(packet.data(),packet.size());
        DiscoveryInfo info{};
        if(got!=n || !parse_discovery_info(packet.data(),static_cast<size_t>(got),info) ||
           info.device.id!=id.device_id || info.device.protocol!=id.transport)continue;
        cache.appliance_type=info.device_type;cache.port=info.port;
        cache.profile.model=info.model;cache.profile.model_known=true;
        cache.profile.subtype=info.subtype;cache.profile.subtype_known=info.subtype_known;
        // These are exact layout exceptions in the pinned upstream AC driver.
        cache.profile.new_temperature_layout=std::strcmp(info.model.data(),"22013279")==0;
        if(info.subtype_known && info.subtype==1 &&
           std::strcmp(info.model.data(),"23096633")==0)
            cache.profile.family=AcProtocol::Subprotocol;
        cache.discovery_attempts=2;
        break;
    }
    udp.stop();
}

class LanSession {
public:
    LanSession(uint64_t device_id,const Credential* credentials):id_(device_id),creds_(credentials){}
    ~LanSession(){tcp_.stop();}
    ReadStatus open(const IPAddress& ip,uint16_t port) {
        uint8_t stamp[8]{};
        if(!now_stamp(stamp))return ReadStatus::TimeNotReady;
        tcp_.setTimeout(read_timeout_ms);
        if(!tcp_.connect(ip,port,1100))return ReadStatus::ConnectionFailed;
        if(!creds_)return ReadStatus::Ok; // V2 never enters a V3 handshake.
        if(!send_bytes(tcp_,handshake_request(*creds_,0)))return ReadStatus::ConnectionFailed;
        Bytes packet;bool broken=false;
        if(!read_packet(tcp_,true,packet,Budget{},broken) ||
           !handshake_response(packet.data(),packet.size(),*creds_,key_))
            return ReadStatus::AuthenticationFailed;
        // Required by the already field-tested V3 read path.
        vTaskDelay(pdMS_TO_TICKS(1100));
        return ReadStatus::Ok;
    }
    bool send(const Bytes& command) {
        uint8_t stamp[8]{};Bytes inner,request;
        if(!healthy_ || !now_stamp(stamp) ||
           !inner_encode(id_,command.data(),command.size(),stamp,inner))return false;
        if(creds_) {
            uint8_t padding[15]{};esp_fill_random(padding,sizeof(padding));
            if(!encrypted_request(inner.data(),inner.size(),key_,sequence_++,
                                  padding,sizeof(padding),request))return false;
        } else request.swap(inner);
        if(!send_bytes(tcp_,request)){healthy_=false;return false;}
        return true;
    }
    bool receive(Bytes& frame,const Budget& budget) {
        Bytes packet,inner;bool broken=false;
        if(!read_packet(tcp_,creds_!=nullptr,packet,budget,broken)) {
            if(broken || !tcp_.connected())healthy_=false;
            return false;
        }
        if(creds_) {
            if(!encrypted_response(packet.data(),packet.size(),key_,inner)) {
                last_v3_read_detail_code.store(20);healthy_=false;return false;
            }
        } else inner.swap(packet);
        // V3 authentication alone must not accept another inner appliance ID.
        if(!v2_packet_matches_device(inner.data(),inner.size(),id_) ||
           !inner_decode(inner.data(),inner.size(),frame)) {
            last_v3_read_detail_code.store(21);healthy_=false;return false;
        }
        if(frame.size()>10)last_v3_reply_opcode_code.store(frame[10]);
        return true;
    }
    bool query(const Bytes& command,State& state,ResponseKind wanted,uint32_t timeout_ms) {
        Budget budget{static_cast<uint32_t>(millis()),timeout_ms};
        if(command.empty() || !timeout_ms || !send(command))return false;
        for(unsigned packet=0;packet<8 && budget.left();++packet) {
            Bytes frame;
            if(!receive(frame,budget))return false;
            if(!matches_query(command,frame,state.profile))continue;
            const auto kind=ingest_ac_response(frame.data(),frame.size(),state);
            if(kind==wanted) {
                last_v3_read_detail_code.store(0);return true;
            }
            if(kind==ResponseKind::Invalid)
                last_v3_read_detail_code.store(parse_state_failure_code(frame.data(),frame.size()));
            // Metadata/notifications of another family do not answer this query.
        }
        return false;
    }
    void drain(uint32_t timeout_ms) {
        Budget budget{static_cast<uint32_t>(millis()),timeout_ms};
        for(unsigned packet=0;packet<3 && budget.left();++packet) {
            Bytes ignored;if(!receive(ignored,budget))break;
        }
    }
    void close(){tcp_.stop();}
    bool healthy()const{return healthy_;}
private:
    WiFiClient tcp_;
    uint64_t id_;
    const Credential* creds_;
    std::array<uint8_t,32> key_{};
    uint16_t sequence_=1;
    bool healthy_=true;
};

ReadStatus read_model(const char* host,uint64_t device_id,const Credential* creds,
                      State& reading,bool negotiate,uint32_t expected_configuration_generation,
                      uint32_t requested_fields=0) {
    last_v3_read_detail_code.store(0);last_v3_reply_opcode_code.store(0);
    IPAddress ip;
    if(!host || !ip.fromString(host) || !allowed_lan_address(ip))return ReadStatus::InvalidAddress;
    if(!device_id)return ReadStatus::InvalidCredentials;
    const ReadIdentity expected=identity(ip,device_id,creds!=nullptr,expected_configuration_generation);
    if(!identity_current(expected))return ReadStatus::ResourceBusy;
    bind_cache(expected);
    Budget overall{static_cast<uint32_t>(millis()),max_read_ms};
    if(negotiate)discover_identity(ip,expected);
    if(cache.appliance_type && cache.appliance_type!=0xac)return ReadStatus::UnsupportedState;
    LanSession session(device_id,creds);
    const ReadStatus opened=session.open(ip,cache.port);
    if(opened!=ReadStatus::Ok)return opened;
    State sample{};sample.has_basic=false;sample.profile=cache.profile;
    uint8_t message_id=1;
    auto query=[&](const Bytes& request,ResponseKind kind,uint32_t budget)->bool {
        if(!identity_current(expected) || !session.healthy())return false;
        return session.query(request,sample,kind,std::min(budget,overall.left()));
    };
    bool basic=query(ac_state_query(sample.profile,message_id++),ResponseKind::BasicState,2000);
    const bool current_c0=basic && sample.profile.family==AcProtocol::Legacy;
    if(!basic && session.healthy() && sample.profile.family==AcProtocol::Legacy) {
        // Alternate query is read-only. A validated BB response, never a failed
        // V3 authentication, selects the encapsulated AC command family.
        AcProfile alternate=sample.profile;alternate.family=AcProtocol::Subprotocol;
        basic=query(ac_state_query(alternate,message_id++),ResponseKind::BasicState,1500);
        if(!basic && session.healthy())
            basic=query(ac_auxiliary_query(sample.profile,message_id++,3),ResponseKind::BasicState,800);
    }
    // A full alternate basic response must precede property acquisition, since
    // it replaces basic fields. Retain only C0 fields A0 does not report from
    // this same query cycle; no prior session's state enters the command.
    if(basic && (negotiate || (requested_fields&AcGeneralFields)) &&
       sample.profile.family==AcProtocol::Legacy &&
       (!sample.comfort_mode_known ||
        (!sample.freeze_protection_known &&
         !(sample.profile.capabilities.reports(AcFeature::FreezeProtection) &&
           !sample.profile.capabilities.supports(AcFeature::FreezeProtection))))) {
        const State current=sample;
        if(query(ac_auxiliary_query(sample.profile,message_id++,3),ResponseKind::BasicState,600) &&
           current_c0) {
            if(!sample.has_indoor && current.has_indoor) {
                sample.has_indoor=true;sample.indoor_c=current.indoor_c;
            }
            if(!sample.has_outdoor && current.has_outdoor) {
                sample.has_outdoor=true;sample.outdoor_c=current.outdoor_c;
            }
            sample.independent_aux_heat=current.independent_aux_heat;
            sample.follow_me=current.follow_me;
            sample.fahrenheit=current.fahrenheit;
            sample.humidity=current.humidity;
            sample.independent_aux_heat_known=current.independent_aux_heat_known;
            sample.follow_me_known=current.follow_me_known;
            sample.fahrenheit_known=current.fahrenheit_known;
            sample.target_humidity_known=current.target_humidity_known;
            sample.classic_aux_bit_ambiguous=current.classic_aux_bit_ambiguous;
            // Short A0 can supply comfort while omitting frost already
            // measured by C0. A capability inference cannot erase that bit;
            // an actual later A0 frost observation remains authoritative.
            if(current.freeze_protection_known && !current.freeze_protection_inferred &&
               (!sample.freeze_protection_known || sample.freeze_protection_inferred)) {
                sample.freeze_protection=current.freeze_protection;
                sample.freeze_protection_known=true;sample.freeze_protection_inferred=false;
                sample.properties_known|=ControlFreezeProtection;
            }
        }
    }
    if(basic && sample.profile.family==AcProtocol::Subprotocol) {
        query(ac_auxiliary_query(sample.profile,message_id++,0),ResponseKind::Auxiliary,700);
        query(ac_auxiliary_query(sample.profile,message_id++,1),ResponseKind::Auxiliary,700);
    }
    if(basic && (negotiate || sample.profile.new_temperature_layout ||
                  (requested_fields&AcPropertyFields))) {
        query(ac_auxiliary_query(sample.profile,message_id++,2),ResponseKind::Auxiliary,800);
        if((requested_fields&ControlSelfClean) ||
           (negotiate && sample.profile.capabilities.supports(AcFeature::SelfClean)))
            query(ac_auxiliary_query(sample.profile,message_id++,4),ResponseKind::Auxiliary,600);
        if(requested_fields&ControlRateSelect)
            query(ac_auxiliary_query(sample.profile,message_id++,5),ResponseKind::Auxiliary,600);
    }
    // Read current state before optional metadata. Some AC modules keep an
    // unsupported appliance/B5 query queued after our timeout, delaying C0
    // until the next request. Such late packets still must not answer a
    // different query. Metadata failure must not suppress a timely basic read
    // or its required model-specific B1/BB state supplements.
    // The timed write pre-read uses only the identity-bound cached profile.
    if(negotiate && session.healthy()) {
        constexpr uint32_t probe_budget_ms=800;
        for(unsigned count=0;count<2 && overall.left()>2*probe_budget_ms;++count) {
            const int probe=cache.plan.next_probe();
            if(probe<0)break;
            const Bytes request=probe==0?subtype_query():
                capabilities_query(message_id++,probe==2,sample.profile.protocol_version);
            const bool ok=query(request,probe==0?ResponseKind::Subtype:ResponseKind::Capabilities,
                                probe_budget_ms);
            cache.plan.finish_probe(static_cast<uint8_t>(probe),ok);
            if(!session.healthy())break;
        }
    }
    session.close();
    if(!identity_current(expected))return ReadStatus::ResourceBusy;
    // Only profile metadata persists across sessions. No optional reply can
    // refresh the service's state timestamp when the main query failed.
    cache.profile=sample.profile;
    if(!basic || !sample.has_basic || !valid_state(sample) || !overall.left())
        return session.healthy()?ReadStatus::UnsupportedState:ReadStatus::ProtocolFailed;
    reading=sample;
    return ReadStatus::Ok;
}

ReadStatus write_model(const char* host,uint64_t id,const Credential* creds,
                       const ControlPatch& patch,State& confirmed,
                       uint32_t expected_wifi_generation,uint32_t expected_endpoint_generation,
                       uint32_t expected_configuration_generation,
                       int64_t command_deadline_utc,double approved_min_c,double approved_max_c,
                       const AutomaticWriteContext* automatic,WriteReceipt* receipt) {
    if(receipt)*receipt={};
    if(!patch.changed || (patch.changed&~(AcGeneralFields|AcPropertyFields)))
        return ReadStatus::ProtocolFailed;
    const uint32_t started=static_cast<uint32_t>(millis());
    auto authorized=[&]() {
        return WiFi.status()==WL_CONNECTED &&
            cottage_wifi_link_generation()==expected_wifi_generation &&
            site_settings_endpoint_generation()==expected_endpoint_generation &&
            cottage_midea_configuration_current(expected_configuration_generation) &&
            (!automatic || (cottage_midea_automatic_write_current(
                automatic->authorization_generation,automatic->control_config_generation,
                automatic->site_generation) && cottage_midea_automatic_proposal_current(
                automatic->proposal_action,automatic->proposal_target_c))) &&
            command_window_open(static_cast<int64_t>(time(nullptr)),command_deadline_utc,
                                started,static_cast<uint32_t>(millis()),max_prewrite_ms);
    };
    if(!authorized())return ReadStatus::ResourceBusy;
    State original{};
    const ReadStatus initial=read_model(host,id,creds,original,false,
                                       expected_configuration_generation,patch.changed);
    if(initial!=ReadStatus::Ok)return initial;
    if(automatic && (automatic->power!=original.power || automatic->mode!=original.mode ||
       std::fabs(automatic->target_c-original.setpoint_c)>0.001))
        return ReadStatus::ResourceBusy; // newer local/remote state supersedes the request
    const double target=(patch.changed&ControlTemperature)?patch.setpoint_c:original.setpoint_c;
    if(!std::isfinite(target) || target<approved_min_c || target>approved_max_c)
        return ReadStatus::UnsupportedState;
    Bytes control;
    if(!valid_ac_control_patch(original,patch) ||
       !ac_set_state_command(original,patch,2,control))return ReadStatus::UnsupportedState;
    if(!authorized())return ReadStatus::ResourceBusy;
    IPAddress ip;
    if(!host || !ip.fromString(host) || !allowed_lan_address(ip))return ReadStatus::InvalidAddress;
    LanSession session(id,creds);
    const ReadStatus opened=session.open(ip,cache.port);
    if(opened!=ReadStatus::Ok)return opened;
    // Final authorization check follows all state reads, TCP connect and the
    // V3 stabilization wait. A changed adapter/link or expired command sends no SET.
    if(!authorized())return ReadStatus::ResourceBusy;
    if(receipt)receipt->attempted=true;
    if(!session.send(control))return ReadStatus::ControlNotConfirmed;
    if(receipt)receipt->sent=true;
    // One physical SET only. A dropped/ambiguous ACK never causes a retry.
    session.drain(500);
    session.close();
    State fresh{};
    const ReadStatus checked=read_model(host,id,creds,fresh,false,
                                       expected_configuration_generation,patch.changed);
    if(cottage_wifi_link_generation()!=expected_wifi_generation ||
       site_settings_endpoint_generation()!=expected_endpoint_generation ||
       !cottage_midea_configuration_current(expected_configuration_generation) ||
       checked!=ReadStatus::Ok || !ac_control_confirmed(original,fresh,patch))
        return ReadStatus::ControlNotConfirmed;
    confirmed=fresh;
    return ReadStatus::Ok;
}
} // namespace

uint8_t last_v3_read_detail(){return last_v3_read_detail_code.load();}
uint8_t last_v3_reply_opcode(){return last_v3_reply_opcode_code.load();}
ReadStatus read_device_v2(const char* host,uint64_t id,State& reading,uint32_t configuration_generation) {
    return read_model(host,id,nullptr,reading,true,configuration_generation);
}
ReadStatus read_device(const char* host,const Credential& credentials,State& reading,
                       uint32_t configuration_generation) {
    return read_model(host,credentials.device_id,&credentials,reading,true,configuration_generation);
}
ReadStatus write_device_v2(const char* host,uint64_t id,const ControlPatch& patch,State& confirmed,
                           uint32_t expected_wifi_generation,uint32_t expected_endpoint_generation,
                           uint32_t expected_configuration_generation,
                           int64_t command_deadline_utc,double approved_min_c,double approved_max_c,
                       const AutomaticWriteContext* automatic,WriteReceipt* receipt) {
    return write_model(host,id,nullptr,patch,confirmed,expected_wifi_generation,
                       expected_endpoint_generation,expected_configuration_generation,
                       command_deadline_utc,approved_min_c,approved_max_c,automatic,receipt);
}
ReadStatus write_device(const char* host,const Credential& credentials,
                        const ControlPatch& patch,State& confirmed,
                        uint32_t expected_wifi_generation,uint32_t expected_endpoint_generation,
                        uint32_t expected_configuration_generation,
                        int64_t command_deadline_utc,double approved_min_c,double approved_max_c,
                       const AutomaticWriteContext* automatic,WriteReceipt* receipt) {
    return write_model(host,credentials.device_id,&credentials,patch,confirmed,expected_wifi_generation,
                       expected_endpoint_generation,expected_configuration_generation,
                       command_deadline_utc,approved_min_c,approved_max_c,automatic,receipt);
}
} // namespace enevina::midea
#endif
