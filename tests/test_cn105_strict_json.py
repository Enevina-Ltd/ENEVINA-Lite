"""Strict cJSON parser boundary regression (host; not physical UART proof)."""
import ctypes
import ctypes.util
import pathlib
import unittest


class CN105StrictJsonTests(unittest.TestCase):
    def test_firmware_uses_strict_parse_for_both_layers(self):
        firmware = (pathlib.Path(__file__).resolve().parents[1]
                    / "firmware/cottage/src/main.cpp").read_text()
        self.assertIn("cJSON_ParseWithOpts(source->valuestring,nullptr,true)", firmware)
        self.assertIn("cJSON_ParseWithOpts(raw,nullptr,true)", firmware)
        self.assertNotIn("cJSON* doc=cJSON_Parse(source->valuestring);", firmware)
        self.assertNotIn("cJSON* root=cJSON_Parse(raw);", firmware)

    def test_cjson_strict_parser_rejects_trailing_payload(self):
        name = ctypes.util.find_library("cjson")
        if not name:
            self.skipTest("host libcjson not installed")
        lib = ctypes.CDLL(name)
        lib.cJSON_ParseWithOpts.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int]
        lib.cJSON_ParseWithOpts.restype = ctypes.c_void_p
        lib.cJSON_Delete.argtypes = [ctypes.c_void_p]
        envelope = b'{"schema":1,"boot":77,"seq":101}'
        def accepted(data):
            parsed = lib.cJSON_ParseWithOpts(data, None, 1)
            if parsed:
                lib.cJSON_Delete(parsed)
            return bool(parsed)
        self.assertTrue(accepted(envelope))
        self.assertTrue(accepted(envelope + b"  \t\n"))
        for malformed in (envelope + b"garbage", envelope + envelope,
                          envelope + b" 0", envelope + b" []", envelope + b" {"):
            with self.subTest(malformed=malformed):
                self.assertFalse(accepted(malformed))


if __name__ == "__main__":
    unittest.main()
