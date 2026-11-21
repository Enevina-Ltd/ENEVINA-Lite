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
        section=TCP.split('ReadStatus write_model(',1)[1].split('} // namespace',1)[0]
        self.assertEqual(section.count('session.send(control)'),1)
        self.assertIn('ac_set_state_command(original,patch,2,control)',section)
        self.assertIn('ac_control_confirmed(original,fresh,patch)',section)
        compact=''.join(section.split())
        self.assertIn('read_model(host,id,creds,original,false,expected_configuration_generation,patch.changed)',compact)
        self.assertIn('read_model(host,id,creds,fresh,false,expected_configuration_generation,patch.changed)',compact)
        self.assertIn('write_device_v2(',HEADER)
        self.assertIn('write_device(',HEADER)

    def test_all_writes_are_explicit_and_off_setup_ap(self):
        endpoint=SERVICE.split('web.on("/api/midea/control",HTTP_POST',1)[1].split(
            'web.on("/api/midea/test",HTTP_POST',1)[0]
        self.assertIn('cottage_wifi_request_via_ap(web)',endpoint)
        self.assertIn('c23_authorize_local_setting(',endpoint)
        self.assertIn('control_patch_fields_valid(patch)',endpoint)
        self.assertIn('control_request_error(observed,patch)',endpoint)
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
        self.assertIn('read_device_v2(cfg.host,id,sample,before)',worker)
        self.assertIn('write_device_v2(',worker)
        self.assertIn('expected_link,expected_endpoint,before,queued+15,min_c,max_c',worker)
        self.assertIn('read_device(cfg.host,auth,sample,before)',worker)
        self.assertIn('write_device(',worker)
        self.assertIn('NetworkBudgetLease lease',worker)
        self.assertNotIn('cottage_air_service_get_plan(',worker)

    def test_review_p1_p2_control_gates(self):
        wifi=(ROOT/"firmware/cottage/src/cottage_wifi.cpp").read_text()
        self.assertIn('cottage_midea_link_disconnected()',wifi)
        self.assertIn('link_generation.fetch_add(1)',wifi)
        self.assertIn('cottage_wifi_link_generation()!=expected_wifi_generation',TCP)
        self.assertIn('config_updating=true',SERVICE)
        self.assertIn('control_pending.store(false)',SERVICE)
        self.assertIn('web.on("/api/midea/pilot",HTTP_POST',SERVICE)
        self.assertIn('pilot.config_revision==revision.load()',SERVICE)
        self.assertIn('requested>=pilot.min_c && requested<=pilot.max_c',SERVICE)
        self.assertIn('pilot_approved',SERVICE)
        # Wire compatibility defaults and conditional optional readback are
        # covered by executable native regressions, not a source-level demand
        # that every legacy pump report modern optional tail bytes.
        self.assertIn('existing.independent_aux_heat?0x08:0',CODEC)
        self.assertIn('before.independent_aux_heat!=after.independent_aux_heat',CODEC)
        self.assertIn('patch.setpoint_c>40',CODEC)
        for token in ('patch.swing!=12','patch.swing!=15','<option value="12">Pysty</option>',
                      '<option value="3">Vaaka</option>','<option value="15">Molemmat</option>'):
            self.assertIn(token,CODEC if token.startswith('patch.swing') else UI)

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
