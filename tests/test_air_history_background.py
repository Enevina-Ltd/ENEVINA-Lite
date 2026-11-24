"""Exercise actual checkpoint, history route and rebind with an interleaved NVS boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
FW=(ROOT/"firmware/cottage/src/cottage_air_service.cpp").read_text()
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()

class BackgroundAirHistoryTests(unittest.TestCase):
    def test_actual_background_checkpoint_and_binding(self):
        purge=FW.split("bool purge_hourly_history()",1)[1].split("bool save_config()",1)[0]
        checkpoint=FW.split("void checkpoint()",1)[1].split("bool parse_float(",1)[0]
        http=FW.split("void c23_history_http(",1)[1].split("void c23_register_routes(",1)[0]
        bind=FW.split('web.on("/api/air/bind"',1)[1].split('web.on("/api/air/ack"',1)[0]
        code=r'''
#include "cottage/air_humidity.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
using namespace enevina::cottage;
using String=std::string;
constexpr int pdTRUE=1,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,HTTP_POST=1;
constexpr size_t CONTENT_LENGTH_UNKNOWN=size_t(-1);
constexpr uint64_t checkpoint_ms=6ULL*3600*1000,checkpoint_retry_ms=60000;
int semaphore=0;
void* guard=&semaphore;
bool held=false,busy=false,alloc_fail=false,open_fail=false,write_fail=false;
unsigned allocations=0,writes=0,erases=0,saves=0;
uint64_t tick=checkpoint_ms, last_store_ms=0,checkpoint_retry_after_ms=0;
constexpr int64_t utc=1795521600;
bool history_changed=true,history_origin_clean=true,indoor_binding_persisted=true,history_checkpoint_active=false;
uint32_t history_checkpoint_failures=0,stores=0,config_generation=0;
RuuviBinding indoor_binding,outdoor_binding;
AirHistory history;
AirMoisture indoor_air{},outdoor_air{};
bool has_indoor=false,has_outdoor=false;
int64_t indoor_utc=0,outdoor_utc=0;
AdvisoryLimits warning_limits{};
MoistureMonitor alarm_engine{};
std::function<void()> during_write,during_stream;
AirHistoryDump disk{};
bool disk_present=false,save_success=true;
int xSemaphoreTake(void* s,unsigned wait){assert(s==guard&&wait==0);if(busy)return 0;assert(!held);held=true;return pdTRUE;}
void lock(){assert(!held);held=true;}
void unlock(){assert(held);held=false;}
uint64_t uptime_ms(){return tick;}
void delay(int){assert(!held);}
void log_event(int,int){assert(held);}
void* heap_caps_malloc(size_t n,int caps){assert(caps==3);if(alloc_fail)return nullptr;auto* p=std::malloc(n);if(p)++allocations;return p;}
void heap_caps_free(void* p){assert(p&&allocations);--allocations;std::free(p);}
using nvs_handle_t=int;
constexpr int NVS_READWRITE=1,ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1;
int erase_result=ESP_OK,commit_result=ESP_OK,open_result=ESP_OK;
unsigned commits=0,closes=0;
int nvs_open(const char* ns,int mode,nvs_handle_t* out){assert(!std::strcmp(ns,"cottage_hist")&&mode==NVS_READWRITE);*out=5;return open_result;}
int nvs_erase_key(int handle,const char* key){assert(handle==5&&held&&!history_checkpoint_active);assert(!std::strcmp(key,"hourly_v1"));++erases;return erase_result;}
int nvs_commit(int handle){assert(handle==5);++commits;if(commit_result==ESP_OK)disk_present=false;return commit_result;}
void nvs_close(int handle){assert(handle==5);++closes;}
struct Preferences {
    bool begin(const char* ns,bool read){assert(!held&&history_checkpoint_active&&!read&&!std::strcmp(ns,"cottage_hist"));return !open_fail;}
    size_t putBytes(const char* key,const void* data,size_t n){
        assert(!held&&history_checkpoint_active&&!std::strcmp(key,"hourly_v1")&&n==sizeof(disk));
        ++writes;
        if(during_write)during_write();
        if(write_fail)return 0;
        std::memcpy(&disk,data,n);disk_present=true;return n;
    }
    void end(){assert(!held&&history_checkpoint_active);}
};
bool save_config(){assert(!held);++saves;if(save_success){indoor_binding_persisted=true;return true;}return false;}
struct CottageWebServer {
    int code=0;std::string body,retry,which="indoor",mac="02:11:22:33:44:55";
    bool aborted=false;std::function<void()> handler;
    void on(const char*,int,std::function<void()> fn){handler=fn;}
    String arg(const char* key){return !std::strcmp(key,"which")?which:mac;}
    void sendHeader(const char* key,const char* value){if(!std::strcmp(key,"Retry-After"))retry=value;}
    void send(int c,const char*,const char* value){assert(!held);code=c;body=value;}
    void setContentLength(size_t){assert(!held);}
    bool responseAborted()const{return aborted;}
    void streamContent(const char* p){assert(!held);body+=p;if(during_stream){auto cb=std::move(during_stream);cb();}}
};
bool cottage_wifi_require_home_lan(CottageWebServer&){return true;}
bool check_token(CottageWebServer&){return true;}
bool purge_hourly_history()'''+purge+r'''
void checkpoint()'''+checkpoint+r'''
void c23_history_http('''+http+r'''
void register_bind(CottageWebServer& web){web.on("/api/air/bind"'''+bind+r'''}
int main(){
    const std::array<uint8_t,6> old_mac={2,1,2,3,4,5};
    indoor_binding.bind(old_mac);
    assert(history.add(calculate_air_moisture(12.34,50),utc));
    CottageWebServer web;register_bind(web);
    during_write=[&]{
        // A BLE sample and a dashboard read can proceed while NVS writes.
        lock();assert(history.add(calculate_air_moisture(13.56,51),utc+3600));history_changed=true;unlock();
        c23_history_http(web);assert(web.code==200);
        assert(web.body.find("12.34")!=std::string::npos&&web.body.find("13.56")!=std::string::npos);
        web.handler();
        assert(web.code==503&&web.retry=="1"&&web.body.find("HISTORY_BUSY")!=std::string::npos);
        assert(indoor_binding.mac()==old_mac&&erases==0&&saves==0);
    };
    checkpoint();
    assert(!held&&!history_checkpoint_active&&!allocations&&stores==1&&writes==1);
    assert(disk_present&&disk.count==1&&disk.values[0].temperature_centi==1234);
    assert(history.count()==2&&history_changed&&last_store_ms==tick);
    during_write={};tick+=checkpoint_ms;write_fail=true;checkpoint();
    assert(history_changed&&history_checkpoint_failures==1&&stores==1&&writes==2);
    tick+=checkpoint_retry_ms-1;checkpoint();assert(writes==2);
    ++tick;write_fail=false;checkpoint();
    assert(stores==2&&!history_changed&&writes==3&&disk.count==2);
    // An error is not an absent key; no rebind or settings commit.
    erase_result=7;web.handler();
    assert(web.code==500&&indoor_binding.mac()==old_mac&&saves==0&&disk_present);
    erase_result=ESP_OK;commit_result=9;web.handler();
    assert(web.code==500&&indoor_binding.mac()==old_mac&&saves==0);
    commit_result=ESP_OK;save_success=false;web.handler();
    assert(web.code==500&&!disk_present&&!indoor_binding_persisted&&history.count()==0);
    assert(indoor_binding.mac()!=old_mac);
    assert(history.add(calculate_air_moisture(20,50),utc+7200));
    history_changed=true;tick+=checkpoint_ms;checkpoint();
    assert(writes==3&&history_changed);
    save_success=true;web.handler();assert(web.code==200&&indoor_binding_persisted);
    // A read during a ring mutation returns the original coherent copy.
    assert(history.add(calculate_air_moisture(21,50),utc+10800));
    during_stream=[&]{lock();history.clear();history.add(calculate_air_moisture(25,50),utc+14400);unlock();};
    c23_history_http(web);
    assert(web.code==200&&web.body.find("20.00")!=std::string::npos&&web.body.find("21.00")!=std::string::npos);
    assert(web.body.find("25.00")==std::string::npos&&!allocations&&!held);
    busy=true;c23_history_http(web);assert(web.code==503&&web.body.find("HISTORY_BUSY")!=std::string::npos&&!allocations);
    busy=false;alloc_fail=true;c23_history_http(web);assert(web.code==503&&!allocations);
    alloc_fail=false;
    // Untrusted boot-relative data must never be persisted as UTC.
    lock();history.clear();history.add_offline(calculate_air_moisture(10,60),tick);history_changed=true;unlock();
    checkpoint();assert(writes==3);
    assert(history.add(calculate_air_moisture(11,60),utc+18000));
    alloc_fail=true;checkpoint();assert(history_checkpoint_failures==2&&history_changed&&!history_checkpoint_active);
    alloc_fail=false;tick+=checkpoint_retry_ms;open_fail=true;checkpoint();
    assert(history_checkpoint_failures==3&&history_changed&&!allocations);
    tick+=checkpoint_retry_ms;open_fail=false;checkpoint();
    assert(stores==3&&disk.count==1&&!history_changed&&!held&&!allocations);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/"test.cpp").write_text(code)
            subprocess.run(["g++","-std=c++17","-O2","-Wall","-Wextra","-Werror","-UNDEBUG",
                "-I",str(ROOT),str(p/"test.cpp"),str(ROOT/"cottage/air_humidity.cpp"),
                str(ROOT/"cottage/ruuvi_v5.cpp"),"-o",str(p/"test")],check=True)
            subprocess.run([str(p/"test")],check=True,timeout=5)

    def test_only_existing_history_task_performs_periodic_flash_write(self):
        loop=MAIN.split("void loop()",1)[1]
        worker=MAIN.split("static void history_worker(void*)",1)[1].split("static void timeline_endpoint",1)[0]
        tick=FW.split("void c23_tick()",1)[1].split("void c23_checkpoint_tick()",1)[0]
        self.assertIn("c23_checkpoint_tick();",worker)
        self.assertNotIn("c23_checkpoint_tick();",loop)
        self.assertNotIn("checkpoint();",tick)
        self.assertIn("void c23_checkpoint_tick(){checkpoint();}",FW)
        self.assertIn('history_checkpoint_active",history_checkpoint_active',FW)
        self.assertIn('history_checkpoint_failures",history_checkpoint_failures',FW)

if __name__=="__main__":unittest.main()
