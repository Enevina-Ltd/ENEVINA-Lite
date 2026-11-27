"""Pin scoped TLS allocation behavior and retain established safety gates."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
MEM=(ROOT/"firmware/cottage/src/cottage_tls_memory.cpp").read_text()
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()
OTA=(ROOT/"firmware/cottage/src/cottage_secure_ota.cpp").read_text()
SHELLY=(ROOT/"firmware/cottage/src/cottage_shelly_discovery.cpp").read_text()

class TlsMemoryWiring(unittest.TestCase):
    def test_scope_is_current_task_only_and_default_unchanged(self):
        self.assertIn("scope_owner.load()==xTaskGetCurrentTaskHandle()",MEM)
        self.assertIn("scope_owner.compare_exchange_strong(empty,self)",MEM)
        self.assertIn("heap_caps_calloc(n,size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)",MEM)
        self.assertIn("heap_caps_calloc(n,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)",MEM)
        self.assertIn("MBEDTLS_SSL_IN_BUFFER_LEN",MEM)
        self.assertIn("MBEDTLS_SSL_OUT_BUFFER_LEN",MEM)
        self.assertIn("if(!p)failures.fetch_add(1)",MEM)
        self.assertIn("mbedtls_platform_set_calloc_free(tls_calloc,tls_free)",MEM)
        self.assertLess(MAIN.index("cottage_tls_memory_init()"), MAIN.index("cottage_wifi_begin(web)"))
    def test_public_forecasts_keep_certificate_and_memory_lease(self):
        for name,source in (("market","Market"),("weather","Weather")):
            data=(ROOT/f"firmware/main/{name}.cpp").read_text()
            self.assertIn("78*1024",data)
            self.assertIn("cfg.cert_pem=",data)
            self.assertLess(data.index("if(!network_lease.held())"), data.index("PublicTlsMemoryScope tls_memory"))
            self.assertLess(data.index("PublicTlsMemoryScope tls_memory"),data.index("esp_http_client_init(&cfg)"))
            self.assertIn("PublicTlsSource::"+source,data)
            self.assertNotIn("skip_cert_common_name_check",data)
    def test_ota_transport_scope_ends_before_flash(self):
        fn=OTA.split("bool update_app(const Release& release) {",1)[1].split("#endif // ENABLED",1)[0]
        transport=OTA.split("RangeResult download_range(",1)[1].split("bool update_app(",1)[0]
        self.assertIn("PublicTlsSource::Ota",transport)
        self.assertLess(transport.index("PublicTlsMemoryScope tls_memory"),transport.index("esp_http_client_init(&cfg)"))
        self.assertIn("~Close(){close_https(client);}",transport)
        self.assertLess(transport.index("PublicTlsMemoryScope tls_memory"),transport.index("struct Close"))
        self.assertLess(fn.index("download_range("),fn.index("esp_ota_begin"))
        self.assertNotIn("PublicTlsMemoryScope",fn)
        self.assertIn("cfg.cert_pem=COTTAGE_OTA_LOCAL_ROOT_CA",OTA)
    def test_shelly_scratch_is_not_reserved_at_boot(self):
        self.assertNotIn("static char body[kBodyMax]",SHELLY)
        self.assertIn("heap_caps_malloc(kBodyMax,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)",SHELLY)
        self.assertIn("if(!body_mem)return;",SHELLY)
        self.assertEqual(SHELLY.count("fetch(path,body,kBodyMax)"),2)

if __name__=="__main__": unittest.main()
