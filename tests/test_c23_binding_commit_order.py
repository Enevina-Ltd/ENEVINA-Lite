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
        purge=route.index('p.remove("hourly_v1")')
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
        self.assertIn("if(!indoor_binding_persisted || !history_changed ||",checkpoint)
        self.assertLess(checkpoint.index("if(!indoor_binding_persisted"),checkpoint.index('prefs.putBytes("hourly_v1"'))
        self.assertLess(checkpoint.index("lock();"),checkpoint.index('prefs.putBytes("hourly_v1"'))
        self.assertGreater(checkpoint.index("unlock();",checkpoint.index('prefs.putBytes("hourly_v1"')),checkpoint.index('prefs.putBytes("hourly_v1"'))
        self.assertIn('history_changed=false;',checkpoint)
        self.assertIn('// NVS failure keeps history_changed set',checkpoint)

    def test_boot_history_recovery_requires_persisted_mac_identity(self):
        init=SRC.split("void c23_initialize() {",1)[1].split("void c23_ble_worker",1)[0]
        self.assertIn('const String persisted_id=prefs.getString("indoor","")',init)
        self.assertIn("stored_indoor_mac_valid=parse_ruuvi_mac(",init)
        self.assertIn("verified_history_origin(",init)
        self.assertIn("if(origin_verified) {",init)
        self.assertIn("history.restore(stored);",init)
        self.assertIn('hp.remove("hourly_v1")',init)
        self.assertIn("if(!old_history_cleared || !save_config())",init)
        self.assertIn("indoor_binding_persisted=false;",init)
        # A was compiled/persisted in an older firmware; reflash with B
        # without a saved MAC must discard unowned legacy history before
        # first persisting B. Never restore or checkpoint through a purge
        # or config-write failure.
        self.assertLess(init.index('hp.remove("hourly_v1")'),
                        init.index("!save_config()"))
        self.assertLess(init.index("if(origin_verified) {"),
                        init.index("history.restore(stored);"))
        self.assertIn("indoor_binding_persisted=origin_verified || !indoor_binding.bound()",init)

if __name__=="__main__":
    unittest.main()
