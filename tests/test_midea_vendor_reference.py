"""Ensure the upstream Midea library is fully present but not linked into ESP32."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VENDOR = ROOT / "third_party" / "midea-local"


class MideaVendorReferenceTest(unittest.TestCase):
    def test_complete_upstream_python_package_and_license(self):
        self.assertTrue((VENDOR / "LICENSE").is_file())
        self.assertIn("MIT License", (VENDOR / "LICENSE").read_text())
        self.assertIn("425bba4a05b74b619d31bf2d8b5a6baf030aa042",
                      (VENDOR / "UPSTREAM.md").read_text())
        self.assertTrue((VENDOR / "pyproject.toml").is_file())
        self.assertTrue((VENDOR / "uv.lock").is_file())
        package = VENDOR / "midealocal"
        modules = list(package.rglob("*.py"))
        self.assertEqual(len(modules), 96)
        self.assertTrue((package / "devices" / "ac" / "message.py").is_file())
        self.assertTrue((package / "devices" / "ac" / "__init__.py").is_file())
        self.assertTrue((package / "cloud.py").is_file())
        for module in modules:
            compile(module.read_text(encoding="utf-8"), str(module), "exec")

    def test_upstream_submodule_references_are_pinned(self):
        submodules = (ROOT / ".gitmodules").read_text()
        self.assertIn("third_party/midea-local/lua", submodules)
        self.assertIn("https://github.com/midea-lan/midea-lua-lib.git", submodules)
        self.assertIn("third_party/midea-local/plugin", submodules)
        self.assertIn("https://github.com/midea-lan/midea-plugin.git", submodules)

    def test_upstream_test_suite_is_present_but_not_auto_loaded(self):
        original_tests = VENDOR / "tests"
        self.assertEqual(len(list(original_tests.rglob("*.py"))), 124)
        self.assertEqual(len(list(original_tests.rglob("*.json"))), 21)

    def test_esp32_firmware_does_not_reference_python_sources(self):
        pio = (ROOT / "firmware" / "cottage" / "platformio.ini").read_text()
        self.assertNotIn("third_party/midea-local", pio)


if __name__ == "__main__":
    unittest.main()
