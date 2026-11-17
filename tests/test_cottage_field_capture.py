import importlib.util
TEST_HOST = ".".join(map(str,(10,219,227,241)))  # Arbitrary synthetic RFC1918 host
import pathlib
import tempfile
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[1] / "tools" / "cottage_field_capture.py"
spec = importlib.util.spec_from_file_location("cottage_capture", SOURCE)
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CottageCaptureTests(unittest.TestCase):
    def test_allowlist(self):
        self.assertTrue(capture.local_url(f"http://{TEST_HOST}/events", "/events"))
        self.assertTrue(capture.local_url(f"http://{TEST_HOST}", ""))
        for url, expected in [
            (f"https://{TEST_HOST}/events", "/events"),
            (f"http://{TEST_HOST}/command", "/events"),
            (f"http://{TEST_HOST}/events?write=1", "/events"),
            ("http://127.0.0.1/events", "/events"),
            ("http://test@example.com/events", "/events"),
            ("http://8.8.8.8/events", "/events"),
            (f"http://{TEST_HOST}/rpc/Switch.Set", ""),
            (f"http://{TEST_HOST}/", ""),
        ]:
            with self.subTest(url=url), self.assertRaises(ValueError):
                capture.local_url(url, expected)

    def test_cn105_fields(self):
        self.assertEqual(capture.normalize_cn105({
            "id": "climate/Mitsubishi Electric MSZ-FD25VA",
            "mode": "OFF", "current_temperature": "12.0",
            "target_temperature": "21.0"
        }), {"field": "climate", "mode": "OFF", "indoor_c": 12.0, "setpoint_c": 21.0})
        self.assertEqual(capture.normalize_cn105({
            "id": "sensor/Compressor Frequency", "value": 19
        }), {"field": "compressor_hz", "value": 19.0})
        self.assertIsNone(capture.normalize_cn105({"id": "sensor/Compressor Frequency", "value": 201}))
        self.assertIsNone(capture.normalize_cn105({
            "id": "climate/Mitsubishi Electric MSZ-FD25VA",
            "mode": "UNKNOWN", "current_temperature": 12,
            "target_temperature": 16
        }))
        self.assertIsNone(capture.normalize_cn105({"id": "control_write", "value": 0}))

    def test_shelly_readonly(self):
        good={"output":True, "apower":165.9, "aenergy":{"total":1890964.952}}
        self.assertEqual(capture.normalize_shelly(good), {
            "relay_on":True,"cottage_total_w":165.9,"cottage_total_wh":1890964.952
        })
        self.assertIsNone(capture.normalize_shelly({"output":"true","apower":10,"aenergy":{"total":10}}))
        self.assertIsNone(capture.normalize_shelly({"output":True,"apower":-1,"aenergy":{"total":0}}))

    def test_http_get_only_and_no_credential_persistence(self):
        text=SOURCE.read_text()
        self.assertIn('method="GET"',text)
        self.assertNotIn("Switch.Set",text.split("def shelly():")[1])
        self.assertIn("NoRedirect",text)
        self.assertNotIn("COTTAGE_WIFI_PASSWORD",text)


if __name__=="__main__":
    unittest.main()
