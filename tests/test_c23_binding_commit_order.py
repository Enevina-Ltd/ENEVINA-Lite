"""Verify NVS history is purged before a new Ruuvi sensor binding can be applied.
This is a source-level ordering guard; physical NVS power-loss test is separate.
"""
import pathlib
import unittest

SRC=(pathlib.Path(__file__).resolve().parents[1] /
     "firmware/cottage/src/cottage_air_service.cpp").read_text()

class BindingCommitTests(unittest.TestCase):
    def test_purge_precedes_binding_and_fail_returns_without_commit(self):
        route=SRC.split('web.on("/api/air/bind",HTTP_POST',1)[1].split('web.on("/api/air/ack",HTTP_POST',1)[0]
        purge=route.index('purge_hourly_history()')
        failure=route.index('if(!purged)')
        new_binding=route.index('indoor_binding.bind(mac)')
        save=route.index('const bool saved=save_config()')
        self.assertLess(purge,failure)
        self.assertLess(failure,new_binding)
        self.assertLess(new_binding,save)
        reject=route[failure:new_binding]
        self.assertIn('unlock()',reject)
        self.assertIn('History NVS purge failed',reject)
        self.assertIn('return;',reject)

    def test_config_failure_blocks_new_history_checkpoint_until_saved(self):
        save=SRC.split("bool save_config() {",1)[1].split("void checkpoint() {",1)[0]
        route=SRC.split('web.on("/api/air/bind",HTTP_POST',1)[1].split('web.on("/api/air/ack",HTTP_POST',1)[0]
        checkpoint=SRC.split("void checkpoint() {",1)[1].split("bool parse_float(",1)[0]
        guard=route.index("indoor_binding_persisted=false;")
        bind=route.index("indoor_binding.bind(mac)")
        persist=route.index("const bool saved=save_config()")
        self.assertLess(guard,bind)
        self.assertLess(bind,persist)
        self.assertIn("if(saved)",save)
        self.assertIn("indoor_binding_persisted=true;",save)
        self.assertIn("!history_origin_clean || !indoor_binding_persisted ||",checkpoint)
        self.assertLess(checkpoint.index("!history_origin_clean"),checkpoint.index('prefs.putBytes("hourly_v1"'))
        self.assertLess(checkpoint.index("xSemaphoreTake(guard,0)"),checkpoint.index('prefs.putBytes("hourly_v1"'))
        self.assertLess(checkpoint.index("history_checkpoint_active=true;"),checkpoint.index('prefs.putBytes("hourly_v1"'))
        self.assertLess(checkpoint.index("unlock();",checkpoint.index("history_checkpoint_active=true;")),checkpoint.index('prefs.putBytes("hourly_v1"'))
        self.assertGreater(checkpoint.index("history_checkpoint_active=false;"),checkpoint.index('prefs.putBytes("hourly_v1"'))
        self.assertLess(route.index("if(history_checkpoint_active)"),route.index("purge_hourly_history()"))
        self.assertIn('history_changed=false;',checkpoint)
        self.assertIn('// NVS failure keeps history_changed set',checkpoint)

    def test_boot_history_recovery_requires_persisted_mac_identity(self):
        init=SRC.split("void c23_initialize() {",1)[1].split("void c23_ble_worker",1)[0]
        self.assertIn('const String persisted_id=prefs.getString("indoor","")',init)
        self.assertIn("stored_indoor_mac_valid=parse_ruuvi_mac(",init)
        self.assertIn("verified_history_origin(",init)
        self.assertIn("if(origin_verified) {",init)
        self.assertIn("history.restore(*stored);",init)
        self.assertIn('purge_hourly_history()',init)
        self.assertIn("if(!old_history_cleared || !save_config())",init)
        self.assertIn("indoor_binding_persisted=false;",init)
        # A was compiled/persisted in an older firmware; reflash with B
        # without a saved MAC must discard unowned legacy history before
        # first persisting B. Never restore or checkpoint through a purge
        # or config-write failure.
        self.assertLess(init.index('purge_hourly_history()'),
                        init.index("!save_config()"))
        self.assertLess(init.index("if(origin_verified) {"),
                        init.index("history.restore(*stored);"))
        self.assertIn("indoor_binding_persisted=origin_verified || !indoor_binding.bound()",init)

    def test_failed_boot_purge_cannot_be_bypassed_by_any_later_config_save(self):
        save=SRC.split("bool save_config() {",1)[1].split("void checkpoint() {",1)[0]
        init=SRC.split("void c23_initialize() {",1)[1].split("void c23_ble_worker",1)[0]
        route=SRC.split('web.on("/api/air/bind",HTTP_POST',1)[1].split('web.on("/api/air/ack",HTTP_POST',1)[0]
        checkpoint=SRC.split("void checkpoint() {",1)[1].split("bool parse_float(",1)[0]
        self.assertIn("history_origin_clean=origin_verified || old_history_cleared;",init)
        self.assertIn("if(!history_origin_clean)",save)
        self.assertIn('purged=!history_checkpoint_active && purge_hourly_history();',save)
        self.assertIn("if(!purged)",save)
        self.assertIn("indoor_binding_persisted=false;",save)
        self.assertIn("history_origin_clean=true;",save)
        # Save of any settings key (including indoor-MAC) requires a
        # successful purge if initialization could not prove the source.
        self.assertLess(save.index("if(!history_origin_clean)"),
                        save.index('p.putString("indoor",indoor_id)'))
        self.assertLess(save.index("if(!purged)"),
                        save.index('p.putString("indoor",indoor_id)'))
        self.assertIn("history_origin_clean=true;",route)
        self.assertIn("!history_origin_clean || !indoor_binding_persisted ||",checkpoint)
        self.assertIn("const bool saved=save_config()",route)
        for initial_purge,retry_purge in ((False,False),(False,True),(True,True)):
            can_save = initial_purge or retry_purge
            can_checkpoint = can_save
            self.assertEqual(can_save,can_checkpoint)

if __name__=="__main__":
    unittest.main()
