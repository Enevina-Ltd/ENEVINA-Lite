"""Protect the physical Midea V3 read-only compatibility correction."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
TRANSPORT=(ROOT/"firmware/cottage/src/cottage_midea.cpp").read_text()
SERVICE=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
PROTOCOL=(ROOT/"pump/midea_v3_protocol.cpp").read_text()

class MideaV3LiveRead(unittest.TestCase):
    def test_post_auth_stabilization_before_read_only_state_query(self):
        start=TRANSPORT.index("ReadStatus read_device(const char* host")
        end=TRANSPORT.index("ReadStatus write_device_v2(",start)
        section=TRANSPORT[start:end]
        self.assertLess(section.index("handshake_response("),
                        section.index("vTaskDelay(pdMS_TO_TICKS(1100));"))
        self.assertLess(section.index("vTaskDelay(pdMS_TO_TICKS(1100));"),
                        section.index("const Bytes query=state_query(1);"))
        self.assertIn("read_8370(tcp,packet)",section)
        self.assertIn("parse_state(frame.data(),frame.size(),value)",section)

    def test_nonsecret_diagnostics_only(self):
        self.assertIn("parse_state_failure_code(",PROTOCOL)
        self.assertIn("last_v3_reply_opcode_code.store(frame[10])",TRANSPORT)
        self.assertIn('read_detail',SERVICE)
        self.assertIn('read_opcode',SERVICE)
        self.assertIn('pilot_ok',SERVICE)
        self.assertNotIn('token_hex',SERVICE.split('void send_status(WebServer& web)',1)[1].split('} // namespace',1)[0])
        self.assertNotIn('key_hex',SERVICE.split('void send_status(WebServer& web)',1)[1].split('} // namespace',1)[0])

    def test_no_automatic_pump_control(self):
        section=TRANSPORT.split('ReadStatus read_device(const char* host',1)[1].split('ReadStatus write_device_v2(',1)[0]
        self.assertNotIn("set_state_command(",section)
        self.assertNotIn("write_device(",section)

if __name__=="__main__":
    unittest.main()
