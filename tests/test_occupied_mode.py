"""Asuttu-mode integration guard; covers every firmware stage, not only UI."""
from pathlib import Path
import unittest

R = Path(__file__).resolve().parents[1]
MODEL = (R / "cottage/air_humidity.hpp").read_text()
ENGINE = (R / "cottage/air_humidity.cpp").read_text()
SERVICE = (R / "firmware/cottage/src/cottage_air_service.cpp").read_text()
MAIN = (R / "firmware/cottage/src/main.cpp").read_text()
UI = (R / "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text()

class OccupiedModeTests(unittest.TestCase):
    def test_distinct_mode_and_target(self):
        self.assertIn("Hold=2,Occupied=3", MODEL)
        self.assertIn("occupied_target_c=21.0", MODEL)
        self.assertIn("cfg.occupied_target_c", ENGINE)
        self.assertIn("cfg.hold_target_c", ENGINE)
        self.assertIn('p.putFloat("occupied"', SERVICE)
        self.assertIn('prefs.getFloat("occupied",21.0f)', SERVICE)
        self.assertIn('cJSON_AddNumberToObject(air,"occupied_target_c"', SERVICE)
        self.assertIn('if(m<=3)', SERVICE)
        self.assertIn('mode_s!="2"&&mode_s!="3"', SERVICE)

    def test_connected_to_optimizer_and_actuator_selection(self):
        self.assertIn("mode.mode==CottageMode::Occupied?", MAIN)
        self.assertIn("mode.occupied_target_c", MAIN)
        self.assertIn("candidate.heating_mode", MAIN)
        self.assertIn("mode.mode!=CottageMode::Occupied", MAIN)
        self.assertIn("c23_config_generation()!=config_generation", MAIN)
        self.assertIn("cottage_control_tick()", MAIN)

    def test_user_controls_and_backward_compatibility(self):
        self.assertIn('data-mode="3">Asuttu</button>', UI)
        self.assertIn('id="setting-occupied"', UI)
        self.assertIn("a.occupied_target_c", UI)
        self.assertIn("occupied:String(occupied)", UI)
        self.assertIn("const modes=['Kylmillään','Kuivanapito','Ylläpitolämpö','Asuttu']", UI)
        self.assertIn('if(web.hasArg("occupied"))', SERVICE)
        self.assertIn("occupied=config.occupied_target_c", SERVICE)
        self.assertIn("++config_generation", SERVICE)

if __name__=="__main__":
    unittest.main()
