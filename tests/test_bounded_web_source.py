"""Keep the actual ESP32 WebServer transport bounded, without new watchdogs."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
FW=ROOT/'firmware/cottage'
class BoundedWebSource(unittest.TestCase):
    def test_sdk_virtual_write_hooks(self):
        h=(FW/'include/cottage_bounded_web.hpp').read_text()
        c=(FW/'src/cottage_bounded_web.cpp').read_text()
        self.assertIn('_currentClientWrite(const char*,size_t) override',h)
        self.assertIn('_currentClientWrite_P(PGM_P,size_t) override',h)
        self.assertIn('start_ms_=millis();',c)
        self.assertNotIn('WebServer::handleClient();',c)
        self.assertIn('_handleRequest();',c)
        self.assertIn('::send(socket_fd,bytes,count,MSG_DONTWAIT)',c)
        self.assertIn('start_ms_,web_response_budget_ms',c)
        self.assertIn('if(sent!=size)abortResponse();',c)
        self.assertIn('_chunked=false;\n    _currentClient.stop();',c)
        self.assertNotIn('esp_task_wdt',c)
    def test_bounded_ingress(self):
        c=(FW/'src/cottage_bounded_web.cpp').read_text()
        self.assertIn('::recv(fd,request_buffer_+ingress_.used(),count,MSG_DONTWAIT)',c)
        self.assertIn('request_expired(millis(),request_started_)',c)
        self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',c)
        self.assertIn('state!=HttpInput::Complete||!prepareRequest()',c)
        self.assertNotIn('_parseRequest(',c)
        self.assertNotIn('_parseArguments(',c)
        self.assertIn('args[i].key==arg.key',c)
        self.assertIn('finishRequest();',c)
    def test_every_stream_uses_explicit_chunk_transport(self):
        for p in (FW/'src').glob('*.cpp'):
            self.assertNotIn('web.sendContent(',p.read_text(),p.name)
        for name in ('main','cottage_air_service','cottage_history'):
            s=(FW/'src'/f'{name}.cpp').read_text()
            self.assertIn('web.streamContent(',s)
            self.assertIn('web.streamContent("");',s)
        main=(FW/'src/main.cpp').read_text()
        self.assertIn('static CottageWebServer web(80);',main)
        self.assertIn('web_response_aborts',main)
        self.assertIn('web_longest_write_ms',main)
if __name__=='__main__':unittest.main()
