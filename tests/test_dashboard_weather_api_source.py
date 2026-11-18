"""Embedded weather API never invents weather or sends pump writes."""
from pathlib import Path
import unittest
S=(Path(__file__).resolve().parents[1]/
   "firmware/cottage/src/main.cpp").read_text(encoding="utf-8")
A=S.split("static void weather_forecast_endpoint() {",1)[1].split(
    "// C2.5: Small, read-only 15-minute price feed",1)[0]
class WeatherEndpoint(unittest.TestCase):
    def test_route_and_source(self):
        self.assertIn('web.on("/api/weather",HTTP_GET,weather_forecast_endpoint);',S)
        self.assertIn("enevina_firmware::weather_status()",A)
        self.assertIn("enevina_firmware::future_weather(points,max_forecast_points)",A)
        self.assertIn("MALLOC_CAP_SPIRAM",A)
        self.assertIn('\\\"valid\\\"',A)
        self.assertIn('\\\"temperature_c\\\"',A)
    def test_bounded_and_read_only(self):
        self.assertIn("max_forecast_points=64",A)
        self.assertIn("max_emitted_points=48",A)
        self.assertIn("emitted<max_emitted_points",A)
        self.assertIn("point.timestamp_utc<now",A)
        self.assertIn("point.timestamp_utc>now+48*3600",A)
        self.assertIn("std::isfinite(point.temperature_c)",A)
        self.assertNotIn("http.GET",A)
        self.assertNotIn("cn105_bridge_post",A)
        self.assertNotIn("control_armed",A)
        self.assertNotIn("NVS",A)
if __name__=="__main__": unittest.main()
