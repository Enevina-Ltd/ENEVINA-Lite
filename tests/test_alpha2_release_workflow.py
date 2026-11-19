"""Regression guards for the one-time, source-only alpha.2 publishing job.

These tests inspect the exact shell block that GitHub Actions will run.
No network or GitHub credentials are needed.
"""
from pathlib import Path
import subprocess
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = yaml.load(
    (ROOT / ".github/workflows/publish-alpha2-source.yml").read_text(encoding="utf-8"),
    Loader=yaml.BaseLoader,
)
PUBLISH = WORKFLOW["jobs"]["publish-source-prerelease"]
SCRIPT = PUBLISH["steps"][-1]["run"]


class Alpha2ReleaseSafety(unittest.TestCase):
    def test_only_main_push_may_publish(self):
        triggers = WORKFLOW["on"]
        self.assertEqual(triggers["push"]["branches"], ["main"])
        self.assertEqual(
            triggers["push"]["paths"], ["docs/release/v0.1.0-alpha.2.md"]
        )
        self.assertIn("pull_request", triggers)
        self.assertEqual(WORKFLOW["permissions"]["contents"], "read")
        self.assertEqual(PUBLISH["permissions"]["contents"], "write")
        self.assertIn("github.event_name == 'push'", PUBLISH["if"])
        self.assertEqual(PUBLISH["needs"], "verify")
        self.assertEqual(WORKFLOW["jobs"]["verify"]["permissions"] if
                         "permissions" in WORKFLOW["jobs"]["verify"] else
                         WORKFLOW["permissions"]["contents"], "read")

    def test_existing_tag_mismatch_aborts_before_any_release(self):
        self.assertIn("resolve_tag_commit()", SCRIPT)
        self.assertIn('actual_sha="$(resolve_tag_commit)"', SCRIPT)
        self.assertIn('if [[ "$actual_sha" != "$RELEASE_SHA" ]]; then', SCRIPT)
        self.assertIn("exit 1", SCRIPT[
            SCRIPT.index('if [[ "$actual_sha" != "$RELEASE_SHA" ]]; then'):
            SCRIPT.index("gh release create")
        ])
        self.assertLess(
            SCRIPT.index('if [[ "$actual_sha" != "$RELEASE_SHA" ]]; then'),
            SCRIPT.index("gh release create"),
        )
        self.assertIn("while [[ \"$typ\" == tag ]]; do", SCRIPT)
        self.assertIn("--verify-tag", SCRIPT)
        self.assertNotIn("--target", SCRIPT)

    def test_no_firmware_binaries_or_ota_actions(self):
        self.assertIn("v0.1.0-alpha.2", SCRIPT)
        self.assertIn("--prerelease", SCRIPT)
        self.assertIn("--notes-file docs/release/v0.1.0-alpha.2.md", SCRIPT)
        self.assertNotIn("firmware.bin", SCRIPT)
        self.assertNotIn("upload-artifact", SCRIPT)

    def test_publish_script_has_valid_bash_syntax(self):
        result = subprocess.run(["bash", "-n"], input=SCRIPT, text=True,
                                capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
