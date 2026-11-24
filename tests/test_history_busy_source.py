"""Exercise real history HTTP/lookup functions with contended flash guard."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
FW=(ROOT/"firmware/cottage/src/cottage_history.cpp").read_text()
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()
UI=(ROOT/"firmware/cottage/include/cottage_premium_dashboard.hpp").read_text()


class HistoryBusyTests(unittest.TestCase):
    def test_actual_history_readers_do_not_wait_on_flash(self):
        if not shutil.which("g++"):
            self.skipTest("native compiler unavailable")
        lookup=FW.split("bool history_last_event(",1)[1].split("CheckpointRead history_model_read(",1)[0]
        http=FW.split("void history_http(",1)[1].split("} // namespace enevina_firmware",1)[0]
        code=r'''
#include "cottage/history_query.hpp"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <new>
#include <string>
using namespace enevina::cottage;
constexpr int pdTRUE=1,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
constexpr size_t CONTENT_LENGTH_UNKNOWN=size_t(-1);
int lock_marker=0;
void* guard=&lock_marker;
bool held=false,busy=false,fail_alloc=false;
size_t takes=0,gives=0,allocations=0;
int xSemaphoreTake(void* sem,unsigned wait){
    assert(sem==guard && wait==0);++takes;
    if(busy)return 0;
    assert(!held);held=true;return pdTRUE;
}
void xSemaphoreGive(void* sem){assert(sem==guard && held);held=false;++gives;}
void* heap_caps_malloc(size_t n,int flags){
    assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(fail_alloc)return nullptr;
    auto* p=std::malloc(n);if(p)++allocations;return p;
}
void heap_caps_free(void* p){assert(p && allocations);--allocations;std::free(p);}
HistoryStore ring;
HistoryStore* store=&ring;
EventLog events;
bool persistent=true,persistence_healthy=true;
void* persistence=&lock_marker;
std::atomic<uint32_t> history_busy_reads{0};
struct Market{const char* currency="EUR";};
Market market_status(){assert(!held);return {};}
struct SiteSettings{int shelly_scope=1;};
SiteSettings site_settings_snapshot(){assert(!held);return {};}
const char* shelly_scope_name(int){return "heat_pump";}
void delay(int){assert(!held);}
struct CottageWebServer{
    int code=0;std::string body;std::string retry;std::string range="24h";
    void sendHeader(const char* key,const char* val){if(std::string(key)=="Retry-After")retry=val;}
    bool hasArg(const char*)const{return true;}
    std::string arg(const char*)const{return range;}
    void setContentLength(size_t){}
    void send(int c,const char*,const char* data){assert(!held);code=c;body=data;}
    bool responseAborted()const{return false;}
    void streamContent(const char* p){assert(!held);body+=p;}
};
bool history_last_event('''+lookup+r'''
void history_http('''+http+r'''
int main(){
    const auto now=static_cast<uint32_t>(time(nullptr));
    HistoryRecord rec{};rec.slot=now/900;rec.indoor_c100=1234;assert(ring.put(rec));
    const auto kind=static_cast<uint8_t>(HistoryEventKind::UpdateConfirmed);
    HistoryEvent e{};e.utc=now;e.kind=kind;e.arg=2026112403;events.put(e);
    busy=true;uint32_t utc=9,arg=8;
    assert(!history_last_event(kind,utc,arg));
    assert(utc==9 && arg==8 && gives==0);
    CottageWebServer web;history_http(web);
    assert(web.code==503 && web.retry=="1" && web.body.find("HISTORY_BUSY")!=std::string::npos);
    assert(!held && !allocations && gives==0 && history_read_busy_count()==2);
    busy=false;
    assert(history_last_event(kind,utc,arg) && utc==now && arg==2026112403);
    assert(!held && gives==1);
    web={};history_http(web);
    assert(web.code==200 && web.body.find("\"valid\":true")!=std::string::npos);
    assert(web.body.find("\"slots_present\":1")!=std::string::npos);
    assert(web.body.find("12.34")!=std::string::npos);
    assert(web.body.find("\"persistent\":true")!=std::string::npos);
    assert(!held && !allocations && gives==2);
    fail_alloc=true;web={};history_http(web);
    assert(web.code==503 && !allocations && !held && takes==4 && gives==2);
    assert(history_read_busy_count()==2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            cpp=Path(td)/"history.cpp";exe=Path(td)/"history"
            cpp.write_text(code)
            subprocess.run(["g++","-std=c++17","-O2","-Wall","-Wextra","-Werror","-I",str(ROOT),str(cpp),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=5)

    def test_persistence_contract_and_busy_diagnostics(self):
        self.assertIn("const bool ok=persistence->flush(*store,events);",FW)
        self.assertIn("if(xSemaphoreTake(guard,0)!=pdTRUE)",FW)
        self.assertIn("MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT",FW)
        self.assertIn("std::unique_ptr<Scratch,ReleaseScratch>",FW)
        self.assertIn('history_read_busy",enevina_firmware::history_read_busy_count()',MAIN)
        self.assertIn("state.histReadFailed",UI)
        self.assertIn("seq!==state.histRequestSequence||range!==state.histRange",UI)


if __name__=="__main__":
    unittest.main()
