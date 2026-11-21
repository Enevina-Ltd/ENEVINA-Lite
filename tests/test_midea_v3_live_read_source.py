"""Protect the physical Midea V3 read-only compatibility correction."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
TRANSPORT=(ROOT/"firmware/cottage/src/cottage_midea.cpp").read_text()
SERVICE=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
PROTOCOL=(ROOT/"pump/midea_v3_protocol.cpp").read_text()

class MideaV3LiveRead(unittest.TestCase):
    def test_post_auth_stabilization_before_read_only_state_query(self):
        # Authentication and its stabilization wait are shared by both the
        # read and write sessions. Real transport behavior is tested natively.
        section=TRANSPORT.split('ReadStatus open(',1)[1].split('bool send(',1)[0]
        self.assertLess(section.index('handshake_response('),
                        section.index('vTaskDelay(pdMS_TO_TICKS(1100));'))
        reader=TRANSPORT.split('ReadStatus read_model(',1)[1].split('ReadStatus write_model(',1)[0]
        self.assertIn('session.open(ip,cache.port)',reader)
        self.assertIn('ac_state_query(sample.profile,message_id++)',reader)
        self.assertIn('ResponseKind::BasicState',reader)

    def test_nonsecret_diagnostics_only(self):
        self.assertIn("parse_state_failure_code(",PROTOCOL)
        self.assertIn("last_v3_reply_opcode_code.store(frame[10])",TRANSPORT)
        self.assertIn('read_detail',SERVICE)
        self.assertIn('read_opcode',SERVICE)
        self.assertIn('pilot_ok',SERVICE)
        self.assertNotIn('token_hex',SERVICE.split('void send_status(WebServer& web)',1)[1].split('} // namespace',1)[0])
        self.assertNotIn('key_hex',SERVICE.split('void send_status(WebServer& web)',1)[1].split('} // namespace',1)[0])

    def test_no_automatic_pump_control(self):
        section=TRANSPORT.split('ReadStatus read_model(',1)[1].split('ReadStatus write_model(',1)[0]
        self.assertNotIn("set_state_command(",section)
        self.assertNotIn("write_device(",section)

if __name__=="__main__":
    unittest.main()
