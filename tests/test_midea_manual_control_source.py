"""Source-level guardrails before a physical Midea V2/V3 field pilot."""
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
SERVICE=(ROOT/"firmware/cottage/src/cottage_midea_service.cpp").read_text()
TCP=(ROOT/"firmware/cottage/src/cottage_midea.cpp").read_text()
CODEC=(ROOT/"pump/midea_v3_protocol.cpp").read_text()
HEADER=(ROOT/"firmware/cottage/include/cottage_midea.hpp").read_text()
UI=(ROOT/"firmware/cottage/include/cottage_premium_dashboard.hpp").read_text()
CAT=(ROOT/"pump/adapter_catalog.hpp").read_text()
LANG=json.loads(UI.split("const UI_EN=Object.freeze(",1)[1].split(");\nconst UI_EN_FRAGMENTS",1)[0])


class MideaManualControl(unittest.TestCase):
    def test_wire_command_golden_vectors_and_readback(self):
        self.assertIn('bool set_state_command(',CODEC)
        self.assertIn('ControlTemperature',CODEC)
        self.assertIn('existing.follow_me',CODEC)
        self.assertIn('existing.purifier',CODEC)
        self.assertIn('existing.aux_heat',CODEC)
        self.assertIn('existing.freeze_protection',CODEC)
        self.assertIn('bool control_patch_observed(',CODEC)
        self.assertIn('ReadStatus write_device_v2(',TCP)
        self.assertIn('ReadStatus write_device(',TCP)
        self.assertIn('ReadStatus::ControlNotConfirmed',TCP)
        for name in ("v2",""):
            marker=f'ReadStatus write_device_{name}(' if name else 'ReadStatus write_device('
            section=TCP.split(marker,1)[1].split('ReadStatus write_device',1)[0]
            self.assertEqual(section.count('send_bytes(tcp,request)'),1)
            self.assertIn('set_state_command(original,patch,2,control)',section)
            self.assertIn('control_patch_observed(fresh,patch)',section)
        self.assertIn('write_device_v2(',HEADER)
        self.assertIn('write_device(',HEADER)

    def test_all_writes_are_explicit_and_off_setup_ap(self):
        endpoint=SERVICE.split('web.on("/api/midea/control",HTTP_POST',1)[1].split(
            'web.on("/api/midea/test",HTTP_POST',1)[0]
        self.assertIn('cottage_wifi_request_via_ap(web)',endpoint)
        self.assertIn('c23_authorize_local_setting(',endpoint)
        self.assertIn('valid_control_patch(patch)',endpoint)
        self.assertIn('control_pending.store(true)',endpoint)
        self.assertIn('web.send(202',endpoint)
        self.assertNotIn('write_device(',endpoint)
        self.assertNotIn('write_device_v2(',endpoint)
        assert 'control_pending.store(false)' in SERVICE
        self.assertIn('control_queued_utc',SERVICE)
        self.assertIn('now-queued<=15',SERVICE)

    def test_worker_is_serialized_and_no_automatic_midea_control(self):
        worker=SERVICE.split('void cottage_midea_service_worker(void*) {',1)[1]
        self.assertIn('if(control_pending.load())',worker)
        self.assertIn('control_busy=true',worker)
        self.assertIn('action?',worker)
        self.assertIn('read_device_v2(cfg.host,id,sample)',worker)
        self.assertIn('write_device_v2(cfg.host,id,patch,sample)',worker)
        self.assertIn('read_device(cfg.host,auth,sample)',worker)
        self.assertIn('write_device(cfg.host,auth,patch,sample)',worker)
        self.assertIn('NetworkBudgetLease lease',worker)
        self.assertNotIn('cottage_air_service_get_plan(',worker)

    def test_full_dashboard_ui_and_localization(self):
        for x in ('id="midea-command-power"','id="midea-command-temperature"',
                  'id="midea-command-mode"','id="midea-command-fan"',
                  'id="midea-command-swing"','id="midea-command-send"',
                  "'/api/midea/control'"):
            self.assertIn(x,UI)
        self.assertIn('manual_control',SERVICE)
        self.assertIn('FIRMWARE_CONTROL_EXPERIMENTAL',CAT)
        for fi in ("Midean paikallinen ohjaus · V2 / V3",
                   "Lähetä ohjaus","Pumpun ohjaus vahvistettu takaisinluvulla."):
            self.assertIn(fi,LANG)
            self.assertNotEqual(fi,LANG[fi])


if __name__=="__main__":
    unittest.main()
