"""Cross-check V2 config migration and explicit manual control isolation."""
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
SRV=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
TCP=(ROOT/"firmware/cottage/src/cottage_midea.cpp").read_text()
API=(ROOT/"firmware/cottage/include/cottage_midea.hpp").read_text()
UI=(ROOT/"firmware/cottage/include/cottage_premium_dashboard.hpp").read_text()
CAT=(ROOT/"pump/adapter_catalog.hpp").read_text()
CODEC=(ROOT/"pump/midea_v3_protocol.cpp").read_text()
LANG=json.loads(UI.split("const UI_EN=Object.freeze(",1)[1].split(");\nconst UI_EN_FRAGMENTS",1)[0])

class NativeMideaV2(unittest.TestCase):
    def test_explicit_selection_and_no_silent_downgrade(self):
        self.assertIn('proto=="2"?2:proto=="3"?3:0',SRV)
        self.assertIn('if(cfg.protocol==2)',SRV)
        self.assertIn('read_device_v2(cfg.host,id,sample)',SRV)
        self.assertIn('read_device(cfg.host,auth,sample)',SRV)
        self.assertNotIn('read_device_v2(',TCP.split('ReadStatus read_device(const char* host,',1)[1].split('ReadStatus write_device_v2(',1)[0])
        self.assertIn('bool v2_packet_matches_device(',CODEC)

    def test_v2_only_queries_and_rejects_untrusted_data(self):
        v2=TCP.split('ReadStatus read_device_v2(',1)[1].split('ReadStatus read_device(',1)[0]
        self.assertIn('state_query(1)',v2)
        self.assertIn('inner_encode(device_id',v2)
        self.assertIn('read_5a5a(tcp,packet)',v2)
        self.assertIn('v2_packet_matches_device(',v2)
        self.assertIn('inner_decode(',v2)
        self.assertIn('parse_state(',v2)
        self.assertNotIn('handshake_request(',v2)
        self.assertNotIn('encrypted_request(',v2)
        self.assertNotIn('SetState',v2)
        self.assertNotIn('send_control(',v2)
        self.assertIn('allowed_lan_address(ip)',v2)
        self.assertIn('ReadStatus read_device_v2(',API)
        self.assertIn('FirmwareControlExperimental',CAT)

    def test_legacy_v3_settings_stay_v3(self):
        self.assertIn('struct LegacyConfig',SRV)
        self.assertIn('config_v1',SRV)
        self.assertIn('config_v2',SRV)
        self.assertIn('migrated.protocol=3',SRV)
        self.assertIn('preferences.isKey("config_v2")',SRV)
        self.assertIn('read_legacy_v3(preferences,rollback_settings)',SRV)
        self.assertIn('rollback_v3_settings_changed(',SRV)
        self.assertIn('if(ok && dest.protocol==3)',SRV)
        self.assertIn('dest=rollback_settings;',SRV)
        self.assertIn('ok=read_legacy_v3(preferences,dest);',SRV)
        self.assertIn('if(c.protocol==2)',SRV)
        self.assertIn('c.token_hex[0]==0 && c.key_hex[0]==0',SRV)
        self.assertIn('if(version==3)',SRV)
        self.assertIn('next.protocol=version',SRV)
        # Test the V3 upgrade/save/rollback source invariant: the V3 v1 NVS
        # record stays present and receives up-to-date credentials BEFORE
        # writing config_v2, so an older A/B image can still read its key.
        persist=SRV.split('bool persist_config(const Config& c) {',1)[1].split(
            'const char* read_code(',1)[0]
        self.assertIn('if(c.protocol==3)',persist)
        self.assertIn('LegacyConfig fallback{}',persist)
        self.assertIn('std::memcpy(fallback.token_hex,c.token_hex',persist)
        self.assertIn('std::memcpy(fallback.key_hex,c.key_hex',persist)
        self.assertIn('preferences.putBytes("config_v1",&fallback',persist)
        self.assertIn('preferences.putBytes("config_v2",&c',persist)
        self.assertLess(persist.index('preferences.putBytes("config_v1",&fallback'),
                        persist.index('preferences.putBytes("config_v2",&c'))
        self.assertNotIn('preferences.remove("config_v1")',persist)

    def test_dashboard_v2_needs_no_token_and_manual_control_is_explicit(self):
        for value in ("Midea-protokolla","V2 (ei tokenia)",
                      "Midean automaattinen optimointiohjaus ei ole vielä käytössä; manuaalinen ohjaus löytyy Midea-asetuksista.",
                      "V2-yhteys onnistui"):
            self.assertIn(value,LANG)
        self.assertIn("protocol==='3' ? $('midea-token').value.trim() : ''",UI)
        self.assertIn("protocol==='3' ? $('midea-key').value.trim() : ''",UI)
        self.assertIn("!isV3",UI)
        # CSS grid on .field otherwise overrides the HTML hidden attribute.
        self.assertIn(".field[hidden]{display:none!important}",UI)
        self.assertIn('id="midea-protocol"',UI)
        self.assertIn('"protocol":%u',SRV.replace('\\',''))
        self.assertIn('manual_control',SRV)
        self.assertIn('web.on("/api/midea/control",HTTP_POST',SRV)
        self.assertIn('write_device_v2(cfg.host,id,patch,sample)',SRV)
        self.assertIn('write_device(cfg.host,auth,patch,sample)',SRV)

if __name__=="__main__":
    unittest.main()