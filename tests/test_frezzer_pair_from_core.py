"""Core-to-Lite developer pairing must not expose credentials or contact a pump."""
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import frezzer_pair_from_core as pairing


class FrezzerPairing(unittest.TestCase):
    def test_rfc1918_only(self):
        for ip in ("10.0.0.2", "172.21.4.1", "192.168.3.20"):
            self.assertEqual(pairing.private_ip(ip), ip)
        for ip in ("8.8.8.8", "169.254.1.3", "127.0.0.1", "0.0.0.0"):
            with self.assertRaises(ValueError):
                pairing.private_ip(ip)

    def test_stored_core_config_validate_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "test-core.yaml"
            p.write_text("midea_lan:\n  host: 192.168.3.40\n  port: 6444\n"
                         "  device_id: 123456\n  token: '" + "a" * 128 +
                         "'\n  key: '" + "b" * 64 + "'\n")
            got = pairing.core_midea_config(p)
            self.assertEqual(got["host"], "192.168.3.40")
            self.assertEqual(got["device_id"], "123456")
            self.assertEqual(got["protocol"], "3")
            self.assertEqual(len(got["v3_token"]), 128)
            p.write_text(p.read_text().replace("6444", "1234"))
            with self.assertRaises(ValueError):
                pairing.core_midea_config(p)

    def test_no_pump_commands_and_no_secret_output(self):
        cfg = {"host": "192.168.3.40", "device_id": "123456",
               "v3_token": "a" * 128, "v3_key": "b" * 64,
               "protocol": "2"}  # Explicit V3 helper must override this.
        calls = []
        def fake_request(host, method, path, fields=None):
            calls.append((host, method, path, fields))
            return {
                "/api/midea/status": {"selected": True},
                "/api/status": {"air": {"config_token": "test-local-only"}},
                "/api/midea/config": {"saved": True}
            }[path]
        output = io.StringIO()
        with patch.object(pairing, "local_request", side_effect=fake_request):
            with contextlib.redirect_stdout(output):
                pairing.pair("192.168.3.50", cfg)
        self.assertEqual([c[2] for c in calls],
                         ["/api/midea/status", "/api/status", "/api/midea/config"])
        self.assertEqual(calls[2][3]["v3_token"], "a" * 128)
        self.assertEqual(calls[2][3]["protocol"], "3")
        self.assertNotIn("a" * 128, output.getvalue())
        self.assertNotIn("test-local-only", output.getvalue())
        self.assertNotIn("192.168.3.40", output.getvalue())

    def test_refuse_pairing_if_not_selected(self):
        with patch.object(pairing, "local_request", return_value={"selected": False}) as req:
            with self.assertRaises(RuntimeError):
                pairing.pair("192.168.3.50", {})
        req.assert_called_once()


if __name__ == "__main__":
    unittest.main()