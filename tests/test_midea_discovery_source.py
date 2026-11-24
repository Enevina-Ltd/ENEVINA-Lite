"""Owner-initiated, read-only Midea LAN discovery integration guards."""
import unittest
import subprocess
import json
import shutil
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
API=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
UI=(ROOT/"firmware/cottage/include/cottage_premium_dashboard.hpp").read_text()
PARSER=(ROOT/"pump/midea_discovery.hpp").read_text()
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()
HELPER=API.split('using namespace enevina::midea;\nDiscoveryJob discovery_job{};',1)[1].split("void cottage_midea_service_register_routes(",1)[0]
HELPER='namespace {\nusing namespace enevina::midea;\nDiscoveryJob discovery_job{};'+HELPER
class MideaDiscoverySource(unittest.TestCase):
    def test_discovery_selection_behavior(self):
        subprocess.run(["node",str(ROOT/"tests/test_midea_discovery_ui.js")],check=True)

    def test_endpoint_preserves_unknown_replies_and_reports_validated_types(self):
        if not shutil.which("g++"):
            self.skipTest("g++ is required for the native discovery endpoint test")
        endpoint=API.split('web.on("/api/midea/discover"',1)[1].split('web.on("/api/midea/config"',1)[0]
        # Run the real route, with only its LAN/HTTP boundary replaced. Frames
        # use the production encryption and validated metadata parser.
        source=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include "pump/midea_discovery_job.hpp"
#include "pump/midea_v3_protocol.hpp"
using enevina::midea::Bytes;
using namespace enevina::midea;
uint32_t tick=100,link=1,endpoint_generation=1;
unsigned opens=0,closes=0,sends=0,reads=0;
bool budget=true,ota=false,connected=true,send_ok=true,authorized=true;
int fd_live=-1;
uint32_t millis(){return tick;}
uint32_t cottage_wifi_link_generation(){return link;}
uint32_t site_settings_endpoint_generation(){return endpoint_generation;}
constexpr int WL_CONNECTED=3,HTTP_POST=1,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
class IPAddress {
    std::array<uint8_t,4> bytes_{};
public:
    IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d):bytes_{a,b,c,d}{}
    IPAddress(uint32_t raw){std::memcpy(bytes_.data(),&raw,4);}
    uint8_t operator[](size_t n)const{return bytes_[n];}
    explicit operator uint32_t()const{uint32_t out;std::memcpy(&out,bytes_.data(),4);return out;}
};
struct {int status(){return connected?WL_CONNECTED:0;} IPAddress localIP(){return {10,0,0,1};}
    IPAddress subnetMask(){return {255,255,255,0};}} WiFi;
std::deque<std::pair<IPAddress,Bytes>> replies;
int fake_socket(int domain,int kind,int proto){assert(domain==AF_INET&&kind==SOCK_DGRAM&&proto==IPPROTO_UDP&&fd_live<0);++opens;return fd_live=99;}
int fake_close(int fd){assert(fd==fd_live);fd_live=-1;++closes;return 0;}
int fake_setsockopt(int fd,int level,int option,const void*,socklen_t){assert(fd==fd_live&&level==SOL_SOCKET&&option==SO_BROADCAST);return 0;}
int fake_bind(int fd,const sockaddr* addr,socklen_t){assert(fd==fd_live);const auto* a=reinterpret_cast<const sockaddr_in*>(addr);
    assert(a->sin_addr.s_addr==uint32_t(WiFi.localIP())&&a->sin_port==0);return 0;}
ssize_t fake_sendto(int fd,const void* bytes,size_t n,int flags,const sockaddr* addr,socklen_t){
    assert(fd==fd_live&&flags==MSG_DONTWAIT&&n==discovery_request.size()&&!std::memcmp(bytes,discovery_request.data(),n));
    const auto port=ntohs(reinterpret_cast<const sockaddr_in*>(addr)->sin_port);
    assert(port==6445||port==20086);++sends;return send_ok?ssize_t(n):-1;
}
ssize_t fake_recvfrom(int fd,void* out,size_t cap,int flags,sockaddr* addr,socklen_t* length){
    assert(fd==fd_live&&flags==MSG_DONTWAIT&&cap==discovery_packet_max+1);++reads;
    if(replies.empty()){errno=EAGAIN;return -1;}
    auto pair=std::move(replies.front());replies.pop_front();
    auto* peer=reinterpret_cast<sockaddr_in*>(addr);peer->sin_family=AF_INET;
    peer->sin_addr.s_addr=uint32_t(pair.first);*length=sizeof(*peer);
    const auto n=std::min(cap,pair.second.size());std::memcpy(out,pair.second.data(),n);return n;
}
void* heap_caps_malloc(size_t n,int flags){assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return std::malloc(n);}
void heap_caps_free(void* p){std::free(p);}
class String:public std::string {
public:
    using std::string::string;
    String(std::string v):std::string(std::move(v)){}
    bool isEmpty()const{return empty();}
};
struct WebServer {
    int status=0;std::string body,scan_id;std::function<void()> handler;
    void on(const char*,int,std::function<void()> callback){handler=callback;}
    bool hasArg(const char* name){return !std::strcmp(name,"token")||(!std::strcmp(name,"scan_id")&&!scan_id.empty());}
    String arg(const char* name){return !std::strcmp(name,"scan_id")?String(scan_id):String("existing-test-token");}
    void sendHeader(const char*,const char*){}
    void send(int code,const char*,const char* value){status=code;body=value;}
};
bool cottage_wifi_require_home_lan(WebServer&){return true;}
bool cottage_wifi_request_via_ap(WebServer&){return false;}
bool cottage_ota_network_maintenance(){return ota;}
namespace enevina::cottage {bool c23_authorize_local_setting(const char*){return authorized;}}
namespace enevina_firmware {struct NetworkBudgetLease {NetworkBudgetLease(int wait,int,int){assert(wait==0);} bool held(){return budget;}};}
#define socket fake_socket
#define close fake_close
#define setsockopt fake_setsockopt
#define bind fake_bind
#define sendto fake_sendto
#define recvfrom fake_recvfrom
''' + HELPER + r'''
#undef socket
#undef close
#undef setsockopt
#undef bind
#undef sendto
#undef recvfrom
void register_discovery(WebServer& web){web.on("/api/midea/discover"''' + endpoint + r'''}
Bytes packet(uint64_t id,uint8_t type,const char* model,bool subtype=true){
    char name[20]{};std::snprintf(name,sizeof(name),"net_%02x_TEST",type);
    const size_t n=std::strlen(name);Bytes info(subtype?80:57+n,0);
    info[4]=0x2c;info[5]=0x19;std::memcpy(info.data()+17,model,8);
    info[40]=uint8_t(n);std::memcpy(info.data()+41,name,n);
    if(subtype){info[57+n]=0xff;info[58+n]=0xff;}
    Bytes result;uint8_t stamp[8]{};
    assert(enevina::midea::inner_encode(id,info.data(),info.size(),stamp,result));return result;
}
Bytes minimal(uint64_t id){Bytes result(72,0);result[0]=0x5a;result[1]=0x5a;
    for(unsigned i=0;i<6;i++)result[20+i]=uint8_t(id>>(8*i));
    return result;}
void step(uint32_t ms=25){tick+=ms;const auto old=reads;cottage_midea_discovery_tick();assert(reads-old<=discovery_receive_quota);}
void run(){
    WebServer web;register_discovery(web);
    const auto old_sends=sends,old_opens=opens,old_reads=reads;
    web.handler();assert(web.status==202&&sends==old_sends&&opens==old_opens&&reads==old_reads);
    web.scan_id=std::to_string(discovery_job.id());
    web.handler();assert(web.status==202&&sends==old_sends);
    step();assert(sends==old_sends+4&&opens==old_opens+1);
    for(unsigned i=0;i<60;++i){step();}
    web.handler();assert(web.status==200&&fd_live<0);std::cout<<web.body<<'\n';
}
int main(){
    replies.push_back({{10,0,1,2},minimal(50)});
    replies.push_back({{10,0,0,1},minimal(51)});
    auto oversized=minimal(52);oversized.resize(discovery_packet_max+1);
    replies.push_back({{10,0,0,8},oversized});
    replies.push_back({{10,0,0,2},minimal(100)});
    replies.push_back({{10,0,0,2},packet(100,0xac,"NEW0OEM1")});
    // The same ID from another address cannot replace this row's metadata.
    replies.push_back({{10,0,0,9},packet(100,0xcc,"171PNL01")});
    replies.push_back({{10,0,0,3},packet(101,0xcc,"171PNL01")});
    replies.push_back({{10,0,0,4},packet(102,0xc3,"00000001",false)});
    replies.push_back({{10,0,0,5},minimal(103)});run();
    for(unsigned i=0;i<8;i++)replies.push_back({{10,0,0,uint8_t(240+i)},packet(281474976710655-i,0xff,"ZZZZZZZZ")});
    run();
    // Check wraparound and result TTL independently of the fake socket.
    DiscoveryJob wrap;
    const DiscoveryLink local{1,1,{10,0,0,1},{255,255,255,0}};
    const uint32_t near=UINT32_MAX-50;
    const auto id=wrap.start(near,local);
    assert(wrap.start(near+1,local)==id);
    assert(!wrap.queue_expired(near+1));
    assert(wrap.queue_expired(uint32_t(near+discovery_queue_ms)));
    wrap.receiving(near);
    assert(!wrap.receive_expired(uint32_t(near+discovery_receive_ms-1)));
    assert(wrap.receive_expired(uint32_t(near+discovery_receive_ms)));
    wrap.finish(uint32_t(near+discovery_receive_ms));
    assert(wrap.available(uint32_t(near+discovery_receive_ms+1),id));
    assert(!wrap.available(uint32_t(near+discovery_receive_ms+discovery_result_ms),id));
    // Missing budget, changing Wi-Fi, and OTA maintenance cannot produce a
    // false successful empty discovery or re-use an old shortlist.
    WebServer web;register_discovery(web);
    budget=false;web.handler();assert(web.status==202);
    web.scan_id=std::to_string(discovery_job.id());const auto old_sends=sends;
    step(discovery_queue_ms);web.handler();assert(web.status==503&&web.body.find("NETWORK_BUSY")!=std::string::npos&&sends==old_sends);
    budget=true;web.scan_id="";web.handler();assert(web.status==202);step();
    web.scan_id=std::to_string(discovery_job.id());++link;step();
    web.handler();assert(web.status==409&&fd_live<0);
    web.scan_id="";web.handler();assert(web.status==202);step();
    web.scan_id=std::to_string(discovery_job.id());ota=true;step();
    web.handler();assert(web.status==503&&fd_live<0);ota=false;
    web.handler();assert(web.status==503&&web.body.find("OTA_BUSY")!=std::string::npos);
    web.scan_id="";web.handler();assert(web.status==202);step();
    web.scan_id=std::to_string(discovery_job.id());budget=false;
    step(discovery_max_poll_gap_ms+1);web.handler();assert(web.status==503&&fd_live<0);
    budget=true;web.scan_id="";send_ok=false;web.handler();assert(web.status==202);step();
    web.scan_id=std::to_string(discovery_job.id());web.handler();assert(web.status==503&&web.body.find("UDP")!=std::string::npos&&fd_live<0);send_ok=true;
    web.scan_id="0";web.handler();assert(web.status==400);
    web.scan_id="4294967296";web.handler();assert(web.status==400);
    web.scan_id="";authorized=false;web.handler();assert(web.status==403);
    assert(opens==closes);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);cpp=path/"discovery.cpp";binary=path/"discovery"
            cpp.write_text(source)
            subprocess.run(["g++","-std=c++17","-O2","-Wall","-Wextra","-Wformat","-Wformat-security","-Werror","-I",str(ROOT),
                str(cpp),str(ROOT/"pump/midea_discovery_info.cpp"),str(ROOT/"pump/midea_v3_protocol.cpp"),
                "-lcrypto","-o",str(binary)],check=True)
            output=subprocess.check_output([str(binary)],text=True).splitlines()
        mixed,maximum=map(json.loads,output)
        self.assertEqual(mixed["count"],4)
        ac,commercial,hydronic,unknown=mixed["devices"]
        self.assertEqual(ac["ip"],"10.0.0.2")
        self.assertTrue(ac.get("device_type_known"))
        self.assertEqual((ac["device_type"],ac["model"],ac["subtype"]),(172,"NEW0OEM1",65535))
        self.assertTrue(ac["model_known"] and ac["subtype_known"])
        self.assertEqual((commercial["device_type"],commercial["model"]),(204,"171PNL01"))
        self.assertEqual(hydronic["device_type"],195)
        self.assertFalse(hydronic["subtype_known"])
        self.assertFalse(unknown["device_type_known"] or unknown["model_known"] or unknown["subtype_known"])
        self.assertEqual(unknown["device_id"],"103")
        self.assertEqual(maximum["count"],8)
        self.assertFalse(maximum["truncated"])
        self.assertTrue(all(d["model"]=="ZZZZZZZZ" and d["subtype"]==65535 for d in maximum["devices"]))

    def test_endpoint_is_lan_only_and_uses_existing_csrf(self):
        segment=API.split('web.on("/api/midea/discover"',1)[1].split('web.on("/api/midea/config"',1)[0]
        for term in ('HTTP_POST','cottage_wifi_require_home_lan','cottage_wifi_request_via_ap',
                     'c23_authorize_local_setting','cottage_ota_network_maintenance',
                     'discovery_job.start(now,link)','send_discovery_result(web,id)'):
            self.assertIn(term,segment)
        for forbidden in ('persist_config(', 'write_device(', 'send_control(', 'control_pending.store(true)',
                          'NetworkBudgetLease','::recvfrom(','::sendto(','delay('):
            self.assertNotIn(forbidden,segment)

    def test_only_shortlisted_ids_never_auth_or_secrets(self):
        self.assertIn("parse_discovery_reply(",PARSER)
        self.assertIn("out.protocol=protocol",PARSER)
        self.assertIn("out.id=id",PARSER)
        self.assertIn("uint8_t protocol=0",PARSER)
        self.assertIn("cottage_midea_discovery_tick();",MAIN)
        self.assertIn("NetworkBudgetLease lease(0,",HELPER)
        self.assertIn("MSG_DONTWAIT",HELPER)
        self.assertIn("i<discovery_receive_quota",HELPER)
        self.assertNotIn("delay(",HELPER)
        segment=API.split('web.on("/api/midea/discover"',1)[1].split('web.on("/api/midea/config"',1)[0]
        for forbidden in ('v3_token','v3_key','token_hex','key_hex','password','ssid'):
            self.assertNotIn(forbidden,segment)

    def test_ui_manual_selection_without_auto_provisioning(self):
        for term in ('id="midea-discover"', 'id="midea-discover-select"',
                     "selectDiscoveredMidea(", "'/api/midea/discover'",
                     "body:new URLSearchParams(fields)", "request({token,scan_id:String(scanId)})",
                     "showMideaProtocolFields()",
                     "$('midea-token').value=''",
                     "$('midea-key').value=''"):
            self.assertIn(term,UI)
        endpoint=UI.split("fetch('/api/midea/discover'",1)[1].split("});",1)[0]
        self.assertNotIn("'/api/midea/config'",endpoint)
        # Multiple matches must not preselect a real pump without populating
        # the fields; explicitly choosing even the first result fires change.
        self.assertIn("placeholder.value=''",UI)
        self.assertIn("placeholder.selected=true",UI)
        self.assertIn("if(selected==='')return;",UI)

if __name__=='__main__':unittest.main()
