"""Regression: the existing network mutex serializes task-stack allocation."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()
WEATHER=(ROOT/"firmware/main/weather.cpp").read_text()
MARKET=(ROOT/"firmware/main/market.cpp").read_text()

class BootMemoryAdmissionTests(unittest.TestCase):
    def test_actual_network_lease_contention_and_samples(self):
        # Compile the actual firmware implementation with a deterministic
        # semaphore and heap boundary. Keep the production RAII class.
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/"freertos").mkdir()
            (p/"freertos/FreeRTOS.h").write_text("""
#pragma once
#include <cstdint>
constexpr int pdTRUE=1;
#define pdMS_TO_TICKS(x) (x)
""")
            (p/"freertos/semphr.h").write_text("""
#pragma once
#include <cassert>
struct Semaphore { bool held=false; };
using SemaphoreHandle_t=Semaphore*;
inline Semaphore gate_instance;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return &gate_instance;}
inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned){if(s->held)return 0;s->held=true;return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t s){assert(s->held);s->held=false;}
""")
            (p/"esp_heap_caps.h").write_text("""
#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
inline size_t free_bytes=100000,largest_bytes=50000;
inline size_t heap_caps_get_free_size(unsigned caps){return caps==3?free_bytes:0;}
inline size_t heap_caps_get_largest_free_block(unsigned caps){return caps==3?largest_bytes:0;}
""")
            (p/"test.cpp").write_text("""
#include <cassert>
#include "cottage_network_budget.hpp"
#include "esp_heap_caps.h"
using namespace enevina_firmware;
int main(){
 assert(network_budget_init());
 assert(network_budget_min_sampled_free()==0);
 {
   NetworkBudgetLease boot(0,0,0);
   assert(boot.held());
   NetworkBudgetLease tls(0,78*1024,16*1024);
   assert(!tls.held());
   assert(tls.diagnostic().failure==NetworkBudgetFailure::MutexBusy);
   assert(gate_instance.held);
   free_bytes=71000;largest_bytes=35000;
 }
 assert(!gate_instance.held);
 assert(network_budget_min_sampled_free()==71000);
 {
   NetworkBudgetLease tls(0,78*1024,16*1024);
   assert(!tls.held());
   assert(tls.diagnostic().failure==NetworkBudgetFailure::LowMemory);
   assert(!gate_instance.held);
 }
 free_bytes=90000;largest_bytes=45000;
 {
   NetworkBudgetLease tls(0,78*1024,16*1024);
   assert(tls.held());
   NetworkBudgetLease allocation(0,0,0);
   assert(!allocation.held() && gate_instance.held);
   free_bytes=22000;largest_bytes=12000;
   network_budget_sample();
   free_bytes=90000;largest_bytes=45000;
 }
 assert(network_budget_min_sampled_free()==22000);
 assert(network_budget_min_largest_internal()==12000);
 assert(!gate_instance.held);
 assert(network_budget_denials()==3);
 { NetworkBudgetLease retry(0,0,0);assert(retry.held()); }
 assert(!gate_instance.held);
}
""")
            exe=p/"test"
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-UNDEBUG",
                "-I",str(p),"-I",str(ROOT/"firmware/cottage/include"),"-I",str(ROOT),
                str(p/"test.cpp"),str(ROOT/"firmware/cottage/src/cottage_network_budget.cpp"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_boot_lease_covers_all_permanent_task_creation(self):
        setup=MAIN.split("void setup() {",1)[1].split("static void cottage_local_lan_worker(void*) {",1)[0]
        lease=setup.index("NetworkBudgetLease boot_allocation(0,0,0)")
        release=setup.index('boot_memory_sample("startup_released")')
        for token in ("c23_ble_worker,", "start_market_reader();","start_weather_reader(",
                      "history_worker,", "optimizer_worker,", "cottage_ota_worker,", "start_optional_workers(true)"):
            self.assertLess(lease,setup.index(token))
            self.assertLess(setup.index(token),release)
        self.assertLess(setup.index("start_market_reader();"),setup.index("optimizer_worker,"))
        self.assertLess(setup.index("start_weather_reader("),setup.index("history_worker,"))
    def test_runtime_creation_is_zero_wait_and_retryable(self):
        helper=MAIN.split("static void start_optional_workers(",1)[1].split("void setup()",1)[0]
        self.assertIn("!boot_lease_held && !enevina_firmware::network_budget_acquire(0,0,0)",helper)
        self.assertLess(helper.index("++worker_start_deferrals;return;"),helper.index("xTaskCreatePinnedToCore"))
        self.assertIn("if(!boot_lease_held)enevina_firmware::network_budget_release();",helper)
        self.assertIn("weather_retry_reader_start();",MAIN)
        self.assertIn("market_retry_reader_start();",MAIN)
        for text,name in ((WEATHER,"weather_retry_reader_start"),(MARKET,"market_retry_reader_start")):
            body=text.split("void "+name+"() {",1)[1].split("#endif",1)[0]
            self.assertIn("NetworkBudgetLease lease(0,0,0)",body)
            self.assertIn("if(lease.held())",body)
        for filename in ("cottage_geocode.cpp","cottage_shelly_discovery.cpp"):
            source=(ROOT/"firmware/cottage/src"/filename).read_text()
            self.assertLess(source.index("NetworkBudgetLease allocation(0,0,0)"),
                            source.index('xTaskCreate(worker_main'))
    def test_boot_and_network_samples_are_separate(self):
        self.assertIn('"boot_memory"',MAIN)
        self.assertIn('"network_sampled_min_internal"',MAIN)
        self.assertIn('heap_caps_get_minimum_free_size(caps)',MAIN)
        self.assertIn('heap_caps_get_free_size(caps)',MAIN)

if __name__=="__main__":unittest.main()
