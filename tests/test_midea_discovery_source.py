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
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <string>
#include "tests/support/midea_transport/IPAddress.h"
#include "pump/midea_discovery_info.hpp"
#include "pump/midea_v3_protocol.hpp"
using enevina::midea::Bytes;
uint32_t tick=0;
uint32_t millis(){return tick;}
void delay(uint32_t amount){tick+=amount;}
constexpr int WL_CONNECTED=3,HTTP_POST=1;
struct {int status(){return WL_CONNECTED;} IPAddress localIP(){return {10,0,0,1};}
    IPAddress subnetMask(){return {255,255,255,0};}} WiFi;
std::deque<std::pair<IPAddress,Bytes>> replies;
struct WiFiUDP {
    bool begin(unsigned){return true;}
    bool beginPacket(IPAddress,unsigned){return true;}
    void write(const uint8_t*,size_t){} void endPacket(){} void stop(){}
    int parsePacket(){return replies.empty()?0:int(replies.front().second.size());}
    IPAddress remoteIP(){return replies.front().first;}
    void flush(){replies.pop_front();}
    int read(uint8_t* out,size_t maximum){const auto bytes=replies.front().second;replies.pop_front();
        const size_t n=std::min(maximum,bytes.size());std::copy_n(bytes.data(),n,out);return int(n);}
};
struct WebServer {
    int status=0;std::string body;std::function<void()> handler;
    void on(const char*,int,std::function<void()> callback){handler=callback;}
    bool hasArg(const char*){return true;}std::string arg(const char*){return "existing-test-token";}
    void sendHeader(const char*,const char*){}
    void send(int code,const char*,const char* value){status=code;body=value;}
};
bool cottage_wifi_require_home_lan(WebServer&){return true;}
bool cottage_wifi_request_via_ap(WebServer&){return false;}
bool cottage_ota_network_maintenance(){return false;}
namespace enevina::cottage {bool c23_authorize_local_setting(const char*){return true;}}
namespace enevina_firmware {struct NetworkBudgetLease {NetworkBudgetLease(int,int,int){} bool held(){return true;}};}
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
void run(){WebServer web;register_discovery(web);tick=0;web.handler();assert(web.status==200);std::cout<<web.body<<'\n';}
int main(){
    replies.push_back({{10,0,0,2},minimal(100)});
    replies.push_back({{10,0,0,2},packet(100,0xac,"NEW0OEM1")});
    // The same ID from another address cannot replace this row's metadata.
    replies.push_back({{10,0,0,9},packet(100,0xcc,"171PNL01")});
    replies.push_back({{10,0,0,3},packet(101,0xcc,"171PNL01")});
    replies.push_back({{10,0,0,4},packet(102,0xc3,"00000001",false)});
    replies.push_back({{10,0,0,5},minimal(103)});run();
    for(unsigned i=0;i<8;i++)replies.push_back({{10,0,0,uint8_t(240+i)},packet(281474976710655-i,0xff,"ZZZZZZZZ")});
    run();
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);cpp=path/"discovery.cpp";binary=path/"discovery"
            cpp.write_text(source)
            subprocess.run(["g++","-std=c++17","-O2","-Wall","-Wextra","-Werror","-I",str(ROOT),
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
                     'NetworkBudgetLease','udp.endPacket()','udp.stop()'):
            self.assertIn(term,segment)
        for forbidden in ('persist_config(', 'write_device(', 'send_control(', 'control_pending.store(true)'):
            self.assertNotIn(forbidden,segment)

    def test_only_shortlisted_ids_never_auth_or_secrets(self):
        self.assertIn("parse_discovery_reply(",PARSER)
        self.assertIn("out.protocol=protocol",PARSER)
        self.assertIn("out.id=id",PARSER)
        self.assertIn("uint8_t protocol=0",PARSER)
        segment=API.split('web.on("/api/midea/discover"',1)[1].split('web.on("/api/midea/config"',1)[0]
        for forbidden in ('v3_token','v3_key','token_hex','key_hex','password','ssid'):
            self.assertNotIn(forbidden,segment)

    def test_ui_manual_selection_without_auto_provisioning(self):
        for term in ('id="midea-discover"', 'id="midea-discover-select"',
                     "selectDiscoveredMidea(", "'/api/midea/discover'",
                     "body:new URLSearchParams({token})",
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
