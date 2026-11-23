"""ESPHome command POST must carry Content-Length: 0 for empty body."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/"firmware/cottage/src/main.cpp").read_text()
class CN105PostFraming(unittest.TestCase):
    def test_real_esp32_http_command_has_explicit_length(self):
        post=MAIN.split("static bool cn105_bridge_post(",1)[1].split(
            "static void cottage_control_tick(const enevina::PumpRoomState& bridge)",1)[0]
        self.assertIn('req.addHeader("Content-Length","0");',post)
        self.assertIn('req.addHeader("Content-Type","application/x-www-form-urlencoded");',post)
        self.assertIn('const int code=req.POST("");',post)
        self.assertLess(post.index('req.addHeader("Content-Length","0");'),
                        post.index('const int code=req.POST("");'))
        self.assertIn('control_post_http_code.store(code);',post)
        self.assertIn('req.end();',post)
    def test_not_added_to_get_or_unrelated_api(self):
        getter=MAIN.split("static bool cn105_bridge_get(",1)[1].split(
            "static bool cn105_bridge_post(",1)[0]
        self.assertNotIn('req.addHeader("Content-Length","0")',getter)
        self.assertIn('req.GET();',getter)
if __name__ == "__main__": unittest.main()
