// Execute the actual firmware transport against a deterministic in-memory LAN.
// Crypto, packet framing, AC parsers and command planning are real production code.
#include "cottage_midea.hpp"
#include "pump/midea_ac_protocol.hpp"
#include "WiFi.h"
#include "WiFiUdp.h"
#include <cassert>
#include <cstring>
#include <ctime>
#include <iostream>
#include <openssl/evp.h>
using namespace enevina::midea;
static uint32_t configuration_generation=1;
static bool configuration_updating=false;
static uint32_t automatic_generation=7;
static bool cancel_automatic_on_preflight=false;
bool cottage_midea_automatic_write_current(uint32_t authorization,uint32_t control,uint32_t site) {
    return authorization==automatic_generation && control==8 && site==9;
}
uint32_t cottage_midea_configuration_generation(){return configuration_generation;}
bool cottage_midea_configuration_current(uint32_t expected) {
    return !configuration_updating && configuration_generation==expected;
}
bool cottage_midea_automatic_proposal_current(enevina::cottage::BootstrapAction action,double target_c) {
    return action==enevina::cottage::BootstrapAction::HeatSetpoint && target_c==23;
}
uint32_t cottage_wifi_link_generation(){return midea_test::link_generation;}
uint32_t site_settings_endpoint_generation(){return midea_test::endpoint_generation;}
static uint8_t crc(const Bytes& b) {
    uint8_t v=0;for(uint8_t c:b){v^=c;for(int i=0;i<8;++i)v=(v&1)?(v>>1)^0x8c:v>>1;}return v;
}
static Bytes frame(Bytes body,uint8_t type=3) {
    body.push_back(crc(body));Bytes f(10,0);f[0]=0xaa;f[2]=0xac;f[9]=type;
    f[1]=10+body.size();f.insert(f.end(),body.begin(),body.end());
    uint8_t sum=0;for(size_t i=1;i<f.size();++i)sum+=f[i];f.push_back(-sum);return f;
}
static Bytes bb_frame(uint8_t kind,const Bytes& data) {
    Bytes body{0xbb,static_cast<uint8_t>(8+data.size()),0,0xff,0xff,kind};
    body.insert(body.end(),data.begin(),data.end());body.push_back(crc(body));
    uint8_t sum=0;for(uint8_t b:body)sum+=b;body.push_back(-sum);
    Bytes f{0xaa,static_cast<uint8_t>(10+body.size()),0xac,0,0,0,0,0,8,3};
    f.insert(f.end(),body.begin(),body.end());sum=0;
    for(size_t i=1;i<f.size();++i)sum+=f[i];
    f.push_back(-sum);return f;
}
static double target=22;
static unsigned sets=0,cap_queries=0,state_queries=0;
static bool caps_only=false,change_endpoint=false,wrong_id=false,set_echo_only=false;
static bool change_link=false;
static bool a0_model=false,display=false;
static bool a0_only=false,ambiguous_classic_bit=false,change_configuration=false,caps_silent=false;
static bool missing_comfort_tail=false;
static bool short_a0=false,frost_report_unsupported=false;
static bool new_temperature_model=false;
static bool bb_model=false,bb_timer=true,bb_sn8_available=true;
static unsigned bb_sn8_queries=0;
static unsigned property_sets=0;
static uint32_t query_delay=0;
static bool metadata_backlog=false,short_classic=false;
static unsigned backlog_connection=0,late_state_replies=0;
static bool optional_request_pending=false;
static Bytes delayed_state;
static Bytes server(const Bytes& packet) {
    Bytes query;
    if(!inner_decode(packet.data(),packet.size(),query))return {}; // V2 peer; V3 wrapper below.
    if(query.size()<11)return {};
    if(backlog_connection!=midea_test::connection_count) {
        backlog_connection=midea_test::connection_count;
        optional_request_pending=false;delayed_state.clear();
    }
    // Some AC modules keep working on an unsupported query after our receive
    // timeout. A later C0 then arrives during the following BB query. This
    // models that queue, rather than returning a synthetic immediate timeout.
    if(metadata_backlog && !delayed_state.empty()) {
        midea_test::now_ms+=1400;
        ++late_state_replies;
        Bytes late;late.swap(delayed_state);return late;
    }
    if(metadata_backlog && (query[9]==0xa0 || query[10]==0xb5)) {
        if(query[10]==0xb5)++cap_queries;
        optional_request_pending=true;
        return {};
    }
    Bytes reply;
    if(query[10]==0xb5) {
        ++cap_queries;
        if(caps_silent)return {};
        reply=frame({0xb5,3,0x14,0x02,1,1,0x25,0x02,7,32,60,32,60,24,60,1,
                     0x13,0x02,1,static_cast<uint8_t>(a0_model&&!frost_report_unsupported?1:0)});
    } else if(query[10]==0x41) {
        if(bb_model || a0_only)return {}; // Only a validated alternate response selects a family.
        ++state_queries;
        if(change_endpoint)++midea_test::endpoint_generation;
        if(change_link)++midea_test::link_generation;
        if(change_configuration)++configuration_generation;
        if(cancel_automatic_on_preflight)++automatic_generation;
        midea_test::now_ms+=query_delay;
        if(caps_only)reply=frame({0xb5,1,0x14,0x02,1,1});
        else {
            Bytes body(25,0);body[0]=0xc0;body[1]=1;
            body[2]=0x80|uint8_t(int(target)-16)|(target-int(target)>0.1?0x10:0);
            body[3]=102;body[11]=92;body[12]=80;body[19]=40;
            if(a0_model) {
                body[8]=ambiguous_classic_bit?0xc0:0x80;body[10]=4;body[19]=55;
                if(missing_comfort_tail)body[21]=0x80;
                body.resize(missing_comfort_tail?22:20); // Optional comfort/frost tails.
            }
            if(short_classic) {body[1]=0;body.resize(19);}
            reply=frame(body,set_echo_only && sets?2:3);
        }
    } else if(query[10]==0xaa && bb_model) {
        assert(query.size()>16);
        if(query[15]==0x11) {
            Bytes data(37,0);data[0]=1;data[5]=3;data[6]=uint8_t(target*2+30);
            data[7]=102;data[25]=bb_timer?4:0;reply=bb_frame(0x11,data);
        } else if(query[15]==0x10) {
            ++bb_sn8_queries;Bytes data(bb_sn8_available?81:31,0);
            data[7]=0x34;data[8]=8; // 21 C
            if(bb_sn8_available)data[80]=0x31;
            reply=bb_frame(0x10,data);
        } else if(query[15]==0x30) {
            Bytes data(83,0);data[5]=0xe8;data[6]=3;reply=bb_frame(0x30,data);
        } else if(query[15]==0x20) {
            ++sets;target=(int(query[22])-30)/2.0;bb_timer=(query[41]&4)!=0;
            return {};
        } else return {};
    } else if(query[10]==0xa0 && a0_model) {
        Bytes body(25,0);body[0]=0xa0;body[1]=uint8_t((int(target)-12)*2)|1;
        body[2]=0x80;body[3]=102;
        if(missing_comfort_tail)body[14]=1;
        if(short_a0)body.resize(20);
        reply=frame(body,5);
    } else if(query[10]==0xb1) {
        if(new_temperature_model) {
            bool asked=false;
            for(unsigned tag=0;tag<query[11];++tag)
                asked=asked || (query[12+2*tag]==0x7e && query[13+2*tag]==0);
            assert(asked);
            Bytes body{0xb5,1,0x7e,0,42};body.resize(47,0);
            body[6]=28;body[45]=92;body[46]=3; // target25.5, indoor21.3
            reply=frame(body,5);
        } else reply=frame({0xb1,1,0x17,0,0,1,static_cast<uint8_t>(display?100:0)});
    } else if(query[10]==0xb0) {
        ++property_sets;
        size_t cursor=12;
        for(unsigned n=0;n<query[11];++n) {
            assert(cursor+3<query.size());
            const unsigned tag=query[cursor]|unsigned(query[cursor+1])<<8;
            const unsigned length=query[cursor+2];cursor+=3;
            assert(cursor+length<=query.size()-2);
            if(tag==0x17 && length)display=query[cursor]>0;
            cursor+=length;
        }
        return {};
    } else if(query[10]==0x40) {
        ++sets;target=(query[12]&15)+16+((query[12]&16)?0.5:0);
        if(a0_model && !a0_only)assert((query[18]&0x80) && (query[20]&4) &&
                                      !(query[32]&8) && query[29]==55);
        if(a0_only)assert(!(query[18]&0x80) && !(query[20]&4) &&
                          !(query[32]&8) && query[29]==40);
        if(short_classic)assert(query.size()==37 && !(query[11]&1) &&
            (query[12]>>5)==4 && query[13]==102 && (query[17]&15)==0 &&
            query[29]==40 && !(query[31]&0x80) && !(query[32]&9));
        if(missing_comfort_tail)assert((query[32]&1) && ((query[31]&0x80)!=0)==short_a0);
        return {}; // Lost ACK is intentional; only independent readback proves success.
    } else return {};
    Bytes out;uint8_t stamp[8]{};
    assert(inner_encode(wrong_id?999:123456,reply.data(),reply.size(),stamp,out));
    if(metadata_backlog && optional_request_pending && query[10]==0x41) {
        delayed_state=out;return {};
    }
    return out;
}

// Synthetic V3 peer: the handshake vector is shared with the independent
// protocol golden test. No appliance credentials or captured LAN frames are
// used. OpenSSL implements the peer side so real firmware authentication,
// encryption, inner-device binding and response matching all execute here.
static Credential v3_credentials() {
    Credential auth{};auth.device_id=123456;
    for(size_t i=0;i<auth.token.size();++i)auth.token[i]=static_cast<uint8_t>(i);
    for(size_t i=0;i<auth.key.size();++i)auth.key[i]=static_cast<uint8_t>(i+32);
    return auth;
}
static std::array<uint8_t,32> v3_key() {
    auto key=v3_credentials().key;
    for(auto& byte:key)byte^=0x11;
    return key;
}
static Bytes peer_aes(const Bytes& data,bool encrypt) {
    const auto key=v3_key();uint8_t iv[16]{};
    EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new();assert(ctx);
    Bytes result(data.size()+16);int size=0,tail=0;
    assert(EVP_CipherInit_ex(ctx,EVP_aes_256_cbc(),nullptr,key.data(),iv,encrypt?1:0)==1);
    assert(EVP_CIPHER_CTX_set_padding(ctx,0)==1);
    assert(EVP_CipherUpdate(ctx,result.data(),&size,data.data(),static_cast<int>(data.size()))==1);
    assert(EVP_CipherFinal_ex(ctx,result.data()+size,&tail)==1);
    EVP_CIPHER_CTX_free(ctx);result.resize(static_cast<size_t>(size+tail));return result;
}
static Bytes peer_digest(const Bytes& data) {
    Bytes digest(32);unsigned length=0;
    assert(EVP_Digest(data.data(),data.size(),digest.data(),&length,EVP_sha256(),nullptr)==1);
    assert(length==digest.size());return digest;
}
static Bytes v3_server(const Bytes& packet) {
    assert(packet.size()>=8 && packet[0]==0x83 && packet[1]==0x70);
    if((packet[5]&15)==0) {
        const auto auth=v3_credentials();
        assert(packet==handshake_request(auth,0));
        constexpr char response[]="8370004020010000b1e02cf80070f546cf0c6c0a703fe93ccee1034c3ad001f7c9144d863cd92cc902d449a31fbb267c8f352e9968a79e3e5fc95c1bbeaa502fd6454ebde5a4bedc";
        Bytes reply((sizeof(response)-1)/2);
        assert(decode_hex(response,reply.data(),reply.size()));return reply;
    }
    assert((packet[5]&15)==6 && packet.size()>=56);
    const Bytes plain=peer_aes(Bytes(packet.begin()+6,packet.end()-32),false);
    Bytes signed_plain(packet.begin(),packet.begin()+6);
    signed_plain.insert(signed_plain.end(),plain.begin(),plain.end());
    const auto digest=peer_digest(signed_plain);
    assert(std::equal(digest.begin(),digest.end(),packet.end()-32));
    const size_t padding=packet[5]>>4;
    assert(plain.size()>=2+padding);
    const Bytes inner(plain.begin()+2,plain.end()-padding);
    const Bytes response=server(inner);
    if(response.empty())return {};
    const size_t response_padding=(16-(response.size()+2)%16)%16;
    const size_t length=response.size()+response_padding+32;
    Bytes reply{0x83,0x70,static_cast<uint8_t>(length>>8),static_cast<uint8_t>(length),
                0x20,static_cast<uint8_t>((response_padding<<4)|3)};
    Bytes response_plain{0,1};
    response_plain.insert(response_plain.end(),response.begin(),response.end());
    response_plain.resize(response_plain.size()+response_padding,0);
    signed_plain=reply;signed_plain.insert(signed_plain.end(),response_plain.begin(),response_plain.end());
    const auto signature=peer_digest(signed_plain);
    const auto encrypted=peer_aes(response_plain,true);
    reply.insert(reply.end(),encrypted.begin(),encrypted.end());
    reply.insert(reply.end(),signature.begin(),signature.end());return reply;
}
static void reset() {
    ++midea_test::link_generation;++midea_test::endpoint_generation;
    ++configuration_generation;configuration_updating=false;
    midea_test::now_ms=0;midea_test::writes.clear();midea_test::short_write=false;
    midea_test::server=server;midea_test::connected=true;
    target=22;sets=cap_queries=state_queries=property_sets=0;
    a0_model=display=new_temperature_model=false;midea_test::discovery.clear();
    bb_model=false;bb_timer=bb_sn8_available=true;bb_sn8_queries=0;
    a0_only=ambiguous_classic_bit=change_configuration=caps_silent=false;
    missing_comfort_tail=false;
    short_a0=frost_report_unsupported=false;
    caps_only=change_endpoint=wrong_id=set_echo_only=change_link=false;query_delay=0;
    metadata_backlog=short_classic=optional_request_pending=false;
    backlog_connection=late_state_replies=0;delayed_state.clear();
    automatic_generation=7;cancel_automatic_on_preflight=false;
}
static void basic_read_survives_optional_metadata(bool v3) {
    reset();metadata_backlog=true;short_classic=true;
    if(v3)midea_test::server=v3_server;
    const auto auth=v3_credentials();
    State observed{};
    for(unsigned cycle=0;cycle<4;++cycle) {
        const auto started=midea_test::now_ms;
        const auto result=v3?read_device("10.0.0.2",auth,observed,configuration_generation):
            read_device_v2("10.0.0.2",123456,observed,configuration_generation);
        if(result!=ReadStatus::Ok)
            std::cerr<<"Cold metadata acquisition failed: v3="<<v3<<" cycle="<<cycle
                     <<" status="<<static_cast<unsigned>(result)
                     <<" late_C0="<<late_state_replies<<'\n';
        assert(result==ReadStatus::Ok);
        assert(observed.has_basic && observed.setpoint_c==22 && observed.indoor_c==21);
        assert(!observed.profile.capabilities.received && !observed.target_humidity_known);
        assert(midea_test::now_ms-started<=9500 && sets==0 && property_sets==0);
    }
    // Discovery still runs to its bounded end; getting basic state first must
    // not simply disable unsupported feature probes or manufacture support.
    assert(cap_queries==4 && late_state_replies==0);
    // The upstream classic defaults also work through the actual V2/V3
    // transport: exactly one SET, then an independent short-C0 query. Missing
    // optional fields remain unknown in the result; power stays off.
    ControlPatch patch{};patch.changed=ControlTemperature;patch.setpoint_c=23;
    const auto result=v3?write_device("10.0.0.2",auth,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30):write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30);
    assert(result==ReadStatus::Ok && sets==1 && cap_queries==4);
    assert(!observed.power && observed.mode==4 && observed.setpoint_c==23 && observed.fan==102);
    assert(!observed.target_humidity_known && !observed.comfort_mode_known &&
           !observed.freeze_protection_known);
    if(v3) {
        wrong_id=true;observed.setpoint_c=19;
        assert(read_device("10.0.0.2",auth,observed,configuration_generation)==ReadStatus::ProtocolFailed);
        assert(observed.setpoint_c==19 && sets==1);
    }
}
static void automatic_transport_requires_current_permission_and_fresh_state(bool v3) {
    reset();
    if(v3)midea_test::server=v3_server;
    const auto auth=v3_credentials();
    ControlPatch patch{};patch.changed=ControlTemperature;patch.setpoint_c=23;
    State observed{};
    assert((v3?read_device("10.0.0.2",auth,observed,configuration_generation):
        read_device_v2("10.0.0.2",123456,observed,configuration_generation))==ReadStatus::Ok);
    const AutomaticWriteContext request{automatic_generation,8,9,
        observed.power,observed.mode,observed.setpoint_c,enevina::cottage::BootstrapAction::HeatSetpoint,23};
    WriteReceipt receipt{};
    const auto run=[&]() {
        return v3?write_device("10.0.0.2",auth,patch,observed,
            midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
            time(nullptr)+15,12,30,&request,&receipt):
            write_device_v2("10.0.0.2",123456,patch,observed,
            midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
            time(nullptr)+15,12,30,&request,&receipt);
    };
    target=24; // independent remote change with no mode change
    assert(run()==ReadStatus::ResourceBusy && sets==0 && !receipt.attempted);
    target=22;
    cancel_automatic_on_preflight=true;
    assert(run()==ReadStatus::ResourceBusy && sets==0 && !receipt.attempted);
    cancel_automatic_on_preflight=false;automatic_generation=7;
    set_echo_only=true;
    assert(run()==ReadStatus::ControlNotConfirmed && sets==1 &&
           receipt.attempted && receipt.sent && observed.setpoint_c==22);
    target=22;set_echo_only=false;
    assert(run()==ReadStatus::Ok && sets==2 && receipt.sent && observed.setpoint_c==23);
}

int main() {
    automatic_transport_requires_current_permission_and_fresh_state(false);
    automatic_transport_requires_current_permission_and_fresh_state(true);
    basic_read_survives_optional_metadata(false);
    basic_read_survives_optional_metadata(true);
    reset();State observed{};
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(observed.setpoint_c==22 && observed.profile.capabilities.received);
    assert(cap_queries>0 && state_queries>0 && sets==0);
    const auto capabilities_before=cap_queries;
    ControlPatch patch{};patch.changed=ControlTemperature;patch.setpoint_c=23;
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::Ok);
    assert(observed.setpoint_c==23 && sets==1 && cap_queries==capabilities_before);
    // A SET echo is not an independent status response, even on a new socket.
    reset();assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    set_echo_only=true;
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::ControlNotConfirmed);
    assert(sets==1);
    // A selected-device change during the pre-read stops the physical write.
    reset();assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    change_endpoint=true;
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::ResourceBusy);
    assert(sets==0);
    reset();assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    change_configuration=true;
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::ResourceBusy);
    assert(sets==0);
    // Re-saving a previously unresponsive device must restart bounded metadata
    // discovery even when its address, ID, transport and Wi-Fi stay unchanged.
    reset();caps_silent=true;
    for(unsigned cycle=0;cycle<3;++cycle)
        assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    const unsigned exhausted_queries=cap_queries;
    assert(exhausted_queries==4 && !observed.profile.capabilities.received);
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(cap_queries==exhausted_queries);
    ++configuration_generation;caps_silent=false;
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(cap_queries>exhausted_queries && observed.profile.capabilities.received);
    reset();configuration_updating=true;
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::ResourceBusy);
    assert(midea_test::writes.empty());
    reset();assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    change_link=true;
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::ResourceBusy);
    assert(sets==0);
    reset();caps_only=true;observed.setpoint_c=19;
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)!=ReadStatus::Ok);
    assert(observed.setpoint_c==19 && sets==0 && midea_test::now_ms<=9500);
    reset();wrong_id=true;observed.setpoint_c=19;
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::ProtocolFailed);
    assert(observed.setpoint_c==19 && sets==0);
    reset();Credential credentials{};credentials.device_id=123456;
    assert(read_device("10.0.0.2",credentials,observed,configuration_generation)==ReadStatus::AuthenticationFailed);
    assert(midea_test::writes.size()==1 && midea_test::writes[0][0]==0x83);
    reset();
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr),12,30)==ReadStatus::ResourceBusy);
    assert(midea_test::writes.empty());
    reset();query_delay=15000;observed.setpoint_c=19;
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)!=ReadStatus::Ok);
    assert(observed.setpoint_c==19);
    // Selective B0 property writes reach the wire and never emit full-state SET.
    reset();assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert((observed.properties_known&ControlDisplay) && !observed.display);
    patch={};patch.changed=ControlDisplay;patch.display=true;
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::Ok);
    assert(observed.display && property_sets==1 && sets==0);
    // A0 can supply a missing preset; B1 and same-cycle ambient values survive.
    reset();a0_model=true;display=true;
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(observed.freeze_protection_known && observed.has_indoor &&
           observed.indoor_c==21 && (observed.properties_known&ControlDisplay) && observed.display);
    assert(!observed.independent_aux_heat && observed.follow_me && observed.fahrenheit &&
           observed.humidity==55);
    patch={};patch.changed=ControlTemperature;patch.setpoint_c=23;
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::Ok);
    assert(sets==1 && observed.setpoint_c==23 && observed.freeze_protection_known);
    // Complementary tails retain measured C0 frost when short A0 supplies
    // comfort. A capability inference cannot override the measured bit; a
    // genuinely observed later A0 frost value does take precedence.
    for(unsigned variant=0;variant<3;++variant) {
        reset();a0_model=true;missing_comfort_tail=true;
        short_a0=variant!=2;frost_report_unsupported=variant==1;
        assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
        assert(observed.freeze_protection_known && !observed.freeze_protection_inferred &&
               observed.freeze_protection==short_a0 && observed.comfort_mode);
        assert(write_device_v2("10.0.0.2",123456,patch,observed,
            midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
            time(nullptr)+15,12,30)==ReadStatus::Ok);
        assert(sets==1 && observed.comfort_mode && observed.setpoint_c==23);
    }
    // A0-only telemetry uses the upstream wire defaults for omitted fields;
    // those defaults do not become observed state. Conflicting public meanings
    // of an active C0 bit6 still cannot be guessed.
    for(bool only:{false,true}) {
        reset();a0_model=true;a0_only=only;ambiguous_classic_bit=!only;
        assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
        assert(write_device_v2("10.0.0.2",123456,patch,observed,
            midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
            time(nullptr)+15,12,30)==(only?ReadStatus::Ok:ReadStatus::UnsupportedState));
        assert(sets==static_cast<unsigned>(only));
        if(only)assert(observed.setpoint_c==23 && !observed.fahrenheit_known &&
                       !observed.follow_me_known && !observed.target_humidity_known);
        ControlPatch property{};property.changed=ControlDisplay;property.display=true;
        assert(write_device_v2("10.0.0.2",123456,property,observed,
            midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
            time(nullptr)+15,12,30)==ReadStatus::Ok);
        assert(property_sets==1 && sets==static_cast<unsigned>(only));
    }
    // BB writes with an active timer require SN8 from the current BB/10 read,
    // including both timed pre-read and independent readback acquisitions.
    reset();bb_model=true;
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(observed.profile.family==AcProtocol::Subprotocol && observed.bb_timer &&
           observed.bb_sn8_known && observed.bb_sn8);
    const unsigned sn8_before=bb_sn8_queries;
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::Ok);
    assert(sets==1 && bb_timer && observed.bb_timer && observed.setpoint_c==23 &&
           bb_sn8_queries==sn8_before+2);
    reset();bb_model=true;bb_sn8_available=false;
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(write_device_v2("10.0.0.2",123456,patch,observed,
        midea_test::link_generation,midea_test::endpoint_generation,configuration_generation,
        time(nullptr)+15,12,30)==ReadStatus::UnsupportedState);
    assert(sets==0);
    // Exact-model B5/notify2 0x7e temperatures are requested/accepted as live
    // auxiliary data; they are never interpreted as B5 capability flags.
    reset();new_temperature_model=true;
    Bytes info(80,0);info[4]=0x2c;info[5]=0x19;
    std::memcpy(info.data()+17,"22013279",8);
    const char ssid[]="net_ac_TEST";info[40]=sizeof(ssid)-1;
    std::memcpy(info.data()+41,ssid,sizeof(ssid)-1);
    uint8_t stamp[8]{};
    assert(inner_encode(123456,info.data(),info.size(),stamp,midea_test::discovery));
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(observed.profile.model_known && observed.profile.new_temperature_layout);
    assert(observed.setpoint_c==25.5 && observed.has_indoor &&
           std::abs(observed.indoor_c-21.3)<0.001 && !observed.has_outdoor);
    // Internal model codes select only the exact documented layout exception;
    // they are not retail-model names or an allowlist of supported OEMs.
    const auto advertise=[](const char* model,uint16_t subtype,uint8_t type=0xac) {
        Bytes metadata(80,0);metadata[4]=0x2c;metadata[5]=0x19;
        std::memcpy(metadata.data()+17,model,8);
        char network[20]{};std::snprintf(network,sizeof(network),"net_%02x_TEST",type);
        const size_t size=std::strlen(network);metadata[40]=static_cast<uint8_t>(size);
        std::memcpy(metadata.data()+41,network,size);
        metadata[57+size]=static_cast<uint8_t>(subtype);
        metadata[58+size]=static_cast<uint8_t>(subtype>>8);
        uint8_t timestamp[8]{};
        assert(inner_encode(123456,metadata.data(),metadata.size(),timestamp,midea_test::discovery));
    };
    for(const uint16_t subtype:{1u,2u}) {
        reset();bb_model=subtype==1;advertise("23096633",subtype);
        assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
        assert(observed.profile.model_known && observed.profile.subtype==subtype);
        assert(observed.profile.family==(subtype==1?AcProtocol::Subprotocol:AcProtocol::Legacy));
        Bytes first_query;
        assert(inner_decode(midea_test::writes.front().data(),midea_test::writes.front().size(),first_query));
        // Inspect the FIRST query, so fallback discovery cannot mask a missing
        // exact-model selection rule.
        assert(first_query[10]==(subtype==1?0xaa:0x41));
    }
    reset();advertise("22251759",32773);
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(!observed.profile.new_temperature_layout && observed.setpoint_c==22 &&
           observed.has_indoor && observed.indoor_c==21 && observed.has_outdoor && observed.outdoor_c==15);
    reset();advertise("NEW0OEM1",44204);
    assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::Ok);
    assert(observed.profile.family==AcProtocol::Legacy && observed.setpoint_c==22);
    for(const uint8_t type:{0xcc,0xc3}) {
        reset();advertise("171PNL01",1,type);
        assert(read_device_v2("10.0.0.2",123456,observed,configuration_generation)==ReadStatus::UnsupportedState);
        assert(midea_test::writes.empty() && sets==0);
    }
    std::cout<<"Midea firmware transport negotiation PASS\n";
}
