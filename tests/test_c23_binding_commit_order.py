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

if __name__=="__main__":
    unittest.main()
