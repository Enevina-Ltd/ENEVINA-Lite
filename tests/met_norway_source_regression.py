from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
WEATHER=(ROOT/"firmware/main/weather.cpp").read_text(encoding="utf-8")
UI=(ROOT/"firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")
CACHE=(ROOT/"firmware/main/weather_cache.hpp").read_text(encoding="utf-8")
class NorwegianWeather(unittest.TestCase):
 def test_no_fmi_runtime_fetch(self):
    self.assertIn("api.met.no/weatherapi/locationforecast/2.0/compact?",WEATHER)
    self.assertNotIn("opendata.fmi.fi",WEATHER)
    self.assertNotIn("parse_fmi_temperature(",WEATHER)
    self.assertIn("parse_met_norway_temperature(",WEATHER)
    self.assertIn("met_norway_ca_pem",WEATHER)
 def test_tos_compliance(self):
    self.assertIn("EnevinaLite/0.1 (https://enevina.com)",WEATHER)
    self.assertIn("If-Modified-Since",WEATHER)
    self.assertIn("Accept-Encoding",WEATHER)
    self.assertIn("gzip, deflate",WEATHER)
    self.assertIn("HTTP_EVENT_ON_HEADER",WEATHER)
    self.assertIn("met_expand_http(",WEATHER)
    self.assertIn("min_safe_retry",WEATHER)
    self.assertIn("http_status==304",WEATHER)
    self.assertIn("http_status==429",WEATHER)
    self.assertIn("CC BY 4.0",UI)
 def test_location_and_cache_switch(self):
    self.assertIn("weather_cache_version=3",CACHE)
    self.assertIn("location_generation",WEATHER)
    self.assertIn("weather_update_location(",WEATHER)
    self.assertIn("valid_weather_cache(*previous,expected_location",WEATHER)
    self.assertNotIn("new_lat<59",WEATHER)
 def test_weather_is_read_only(self):
    self.assertNotIn("cn105_bridge_post",WEATHER)
    self.assertNotIn("control_armed.store(",WEATHER)
if __name__=="__main__":unittest.main()
