"""Exercise the actual firmware enrollment worker using deterministic host stubs."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SERVICE=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
WORKER=SERVICE.split("static bool process_pairing() {",1)[1].split(
    "bool cottage_midea_service_needs_worker()",1)[0]

HARNESS=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
using namespace std;
namespace enevina::midea {
enum class ReadStatus {Ok,InvalidCredentials,ResourceBusy,AuthenticationFailed};
struct State { bool power=false; };
struct Credential {};
}
using namespace enevina::midea;
struct Config {char host[16]{};char device_id[22]{};char token_hex[129]{};char key_hex[65]{};uint8_t protocol=3;};
struct PairingJob { Config candidate{};uint32_t id=0,revision=0,link=0,endpoint=0,started=0;bool pending=false,succeeded=false,recovering=false;const char* code="not_tested";};
PairingJob pairing;
Config saved;
struct {bool enabled=false;} pilot;
bool configured=true,control_busy=false,config_updating=false,selected=true;
int mutex=0;
constexpr int portMAX_DELAY=0,pdTRUE=1;
void xSemaphoreTake(int,int){}
void xSemaphoreGive(int){}
int pdMS_TO_TICKS(int n){return n;}
int ulTaskNotifyTake(int,int){return 0;}
atomic<uint32_t> revision{1};
atomic<bool> control_pending{false};
atomic<int> last_result{},control_result{};
atomic<int64_t> last_attempt{},last_success{},control_sent_utc{},control_success{},control_queued_utc{};
int pending_patch=0,confirmed_command=0;
struct {void reset(uint32_t){}} midea_mode_history;
State observed;
uint32_t observed_wifi_generation=0,observed_endpoint_generation=0;
uint32_t tick=100,link=1,endpoint=1;
uint32_t millis(){return tick;}
uint32_t cottage_wifi_link_generation(){return link;}
uint32_t site_settings_endpoint_generation(){return endpoint;}
constexpr int WL_CONNECTED=1;
struct {int status(){return WL_CONNECTED;}} WiFi;
bool cottage_ota_network_maintenance(){return false;}
bool is_midea_selected(){return selected;}
int cancels=0,persists=0,reads=0;
void cottage_midea_automatic_cancel(){++cancels;}
bool lease_held=true,persist_ok=true,change_link=false;
namespace enevina_firmware {
struct NetworkBudgetLease {NetworkBudgetLease(unsigned,unsigned,unsigned){}bool held(){return lease_held;}};
}
ReadStatus response=ReadStatus::Ok;
bool decode_device_id(const char*,uint64_t& n){n=123;return true;}
bool decode_credentials(const char*,const char*,const char*,Credential&){return true;}
ReadStatus read_device(const char*,const Credential&,State& s,uint32_t){
 ++reads;s.power=true;if(change_link)++link;return response;
}
ReadStatus read_device_v2(const char* h,uint64_t,State& s,uint32_t r){return read_device(h,Credential{},s,r);}
const char* read_code(int n){return n==int(ReadStatus::AuthenticationFailed)?"authentication_failed":"resource_busy";}
bool persist_config(const Config& c){assert(reads>0);assert(config_updating);assert(pairing.pending);
 ++persists;return persist_ok;}
'''+"static bool process_pairing() {"+WORKER+r'''
void setup(bool recovery=false){
 pairing={};pairing.pending=true;pairing.recovering=recovery;
 pairing.id=1;pairing.revision=revision.load();pairing.link=link;pairing.endpoint=endpoint;pairing.started=tick;
 strcpy(saved.host,"10.0.0.2");strcpy(saved.device_id,"123");
 pairing.candidate=saved;strcpy(pairing.candidate.host,"10.0.0.3");
 reads=persists=cancels=0;response=ReadStatus::Ok;lease_held=true;persist_ok=true;change_link=false;
 pilot.enabled=recovery;
}
int main(){
 setup();response=ReadStatus::AuthenticationFailed;
 assert(process_pairing());assert(reads==1 && persists==0 && !pairing.pending && !pairing.succeeded);
 assert(string(saved.host)=="10.0.0.2");
 setup();change_link=true;
 assert(process_pairing());assert(reads==1 && persists==0 && !pairing.pending);
 setup();lease_held=false;
 assert(process_pairing());assert(pairing.pending && reads==0 && persists==0);
 tick+=25001;assert(process_pairing());assert(!pairing.pending && persists==0);
 setup();persist_ok=false;
 assert(process_pairing());assert(reads==1 && persists==1 && !pairing.succeeded);
 assert(string(saved.host)=="10.0.0.2");
 setup();auto prior=revision.load();
 assert(process_pairing());assert(reads==1 && persists==1 && pairing.succeeded);
 assert(revision.load()==prior+1 && string(saved.host)=="10.0.0.3" && observed.power);
 setup(true);prior=revision.load();
 assert(process_pairing());assert(persists==1 && pilot.enabled && cancels==0);
 assert(revision.load()==prior && string(saved.host)=="10.0.0.3" && last_success.load()==0);
 setup(true);strcpy(pairing.candidate.device_id,"456");
 assert(process_pairing());assert(persists==0 && string(saved.host)=="10.0.0.2");
}
'''
class MideaPairingWorker(unittest.TestCase):
    def test_real_worker_control_flow(self):
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/"pairing.cpp"
            cpp.write_text(HARNESS)
            exe=Path(tmp)/"pairing"
            subprocess.run([os.environ.get("CXX","g++"),"-std=c++17",str(cpp),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_config_route_never_persists_or_controls(self):
        route=SERVICE.split('web.on("/api/midea/config"',1)[1].split('web.on("/api/midea/pilot"',1)[0]
        self.assertNotIn("persist_config(",route)
        self.assertNotIn("write_device",route)
        self.assertIn("pairing.pending=true",route)
        self.assertIn("pilot.enabled",route)
    def test_recovery_requires_same_unambiguous_identity(self):
        recovery=SERVICE.split("void cottage_midea_recovery_tick()",1)[1]
        for item in ("hit.id!=device_id","hit.protocol!=saved.protocol","hit.conflicted","discovery_job.truncated()","revision.load()==scan_revision"):
            self.assertIn(item,recovery)
        self.assertNotIn("write_device",recovery)
        self.assertNotIn("persist_config(",recovery)

if __name__=="__main__":unittest.main()
