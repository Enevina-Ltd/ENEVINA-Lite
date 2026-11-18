import importlib.util
import pathlib
import unittest
import contextlib
import io
import tempfile
from unittest.mock import patch

SOURCE = pathlib.Path(__file__).resolve().parents[1] / "tools" / "check_release_hygiene.py"
SPEC = importlib.util.spec_from_file_location("release_hygiene", SOURCE)
assert SPEC is not None and SPEC.loader is not None
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


class ReleaseHygieneTests(unittest.TestCase):
    def test_private_key_is_blocked(self):
        block = "-----BEGIN " + "OPENSSH PRIVATE KEY-----"
        self.assertIn("private-key-block", MOD.find_issues("notes.md", block))

    def test_github_token_is_blocked(self):
        token = "ghp_" + "Z" * 36
        self.assertIn("possible-access-token", MOD.find_issues("sample.py", token))

    def test_private_site_is_blocked(self):
        name = "Lille" + "villa"
        self.assertIn("identified-private-test-site", MOD.find_issues("docs/field.md", name))

    def test_current_public_example_is_not_secret(self):
        self.assertEqual(
            [],
            MOD.find_issues(
                "firmware/cottage/include/cottage_local_config.example.hpp",
                '#define COTTAGE_WIFI_PASSWORD "your-wifi-password"',
            ),
        )

    def test_private_config_file_is_blocked(self):
        self.assertIn(
            "tracked-private-file-name",
            MOD.find_issues("firmware/cottage/include/cottage_local_config.hpp", ""),
        )

    def test_normal_file_passes(self):
        self.assertEqual([], MOD.find_issues("docs/readme.md", "Generic pump control."))

    def test_pem_private_key_blocks_actual_main_scanner(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            pem = root / "signing.pem"
            pem.write_text("-----BEGIN " + "OPENSSH PRIVATE KEY-----" + "\\nFAKE_TEST_DATA\\n", encoding="utf-8")
            with patch.object(MOD, "ROOT", root), patch.object(
                MOD, "tracked_files", return_value=["signing.pem"]
            ), contextlib.redirect_stdout(io.StringIO()) as output:
                result = MOD.main()
            self.assertEqual(1, result)
            self.assertIn("private-key-block", output.getvalue())

    def test_private_site_in_cpp_blocks_actual_main_scanner(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            source = root / "example.cpp"
            source.write_text("// " + "Lille" + "villa" + "\\n", encoding="utf-8")
            with patch.object(MOD, "ROOT", root), patch.object(
                MOD, "tracked_files", return_value=["example.cpp"]
            ), contextlib.redirect_stdout(io.StringIO()) as output:
                result = MOD.main()
            self.assertEqual(1, result)
            self.assertIn("identified-private-test-site", output.getvalue())

    def test_site_in_json_is_blocked(self):
        self.assertIn(
            "identified-private-test-site",
            MOD.find_issues("examples/test.json", '{"location": "' + "Kala" + "joki" + '"}'),
        )

    def test_noncompliant_env_is_blocked(self):
        self.assertIn(
            "tracked-private-file-name", MOD.find_issues("config/.env.production", "data")
        )


if __name__ == "__main__":
    unittest.main()
