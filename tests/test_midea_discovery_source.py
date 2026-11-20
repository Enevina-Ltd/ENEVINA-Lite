"""Owner-initiated, read-only Midea LAN discovery integration guards."""
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
API=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
UI=(ROOT/"firmware/cottage/include/cottage_premium_dashboard.hpp").read_text()
PARSER=(ROOT/"pump/midea_discovery.hpp").read_text()
class MideaDiscoverySource(unittest.TestCase):
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
