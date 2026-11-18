#!/usr/bin/env python3
"""Static checks for accidentally committed release-sensitive content.

This intentionally scans the current tracked tree only. It does not certify
historic refs, PR refs, binary artifacts, or third-party licensing.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FORBIDDEN_FILE_BASENAMES = {
    ".env", "credentials.json", "secrets.json",
    "cottage_local_config.hpp", "cottage_ota_private.hpp",
}
PRIVATE_SITE_TERMS = ("lille" + "villa", "kala" + "joki", "piha" + "mök")
TOKEN = re.compile(
    r"(?:ghp_|gho_|ghu_|ghs_|ghr_)[A-Za-z0-9]{30,}"
    r"|github_pat_[A-Za-z0-9_]{50,}"
    r"|AKIA[A-Z0-9]{16}"
)
KEY_BLOCK = re.compile(
    r"-----BEGIN\s+(?:(?:RSA|EC|OPENSSH|DSA)\s+)?PRIVATE KEY-----"
)
SECRET_FILE_SUFFIXES = (".p12", ".pfx", ".key")
TEXT_SUFFIXES = {
    ".md", ".txt", ".py", ".cpp", ".c", ".hpp", ".h", ".ino",
    ".ini", ".csv", ".json", ".js", ".html", ".css", ".yml", ".yaml",
    ".pem", ".crt", ".key", ".conf", ".toml", ".properties",
}


def find_issues(path: str, contents: str) -> list[str]:
    """Return category names only: never print a discovered secret."""
    filename = pathlib.PurePosixPath(path).name
    lowered = path.lower()
    issues: list[str] = []
    if (
        filename in FORBIDDEN_FILE_BASENAMES
        or (filename.startswith(".env.") and filename != ".env.example")
        or filename.endswith(SECRET_FILE_SUFFIXES)
        or filename.startswith("private-") and filename.endswith(".pem")
        or filename.endswith("-private.pem")
    ):
        issues.append("tracked-private-file-name")
    if KEY_BLOCK.search(contents):
        issues.append("private-key-block")
    if TOKEN.search(contents):
        issues.append("possible-access-token")
    if any(term in contents.casefold() for term in PRIVATE_SITE_TERMS):
        issues.append("identified-private-test-site")
    return issues


def tracked_files() -> list[str]:
    raw = subprocess.check_output(
        ["git", "ls-files", "-z"], cwd=ROOT
    )
    return [p.decode("utf-8", "surrogateescape") for p in raw.split(b"\0") if p]


def main() -> int:
    findings: list[tuple[str, str]] = []
    scanned = 0
    for rel in tracked_files():
        p = ROOT / rel
        if not p.is_file():
            continue
        # Suspicious filenames must be checked regardless of their contents.
        if p.suffix.lower() not in TEXT_SUFFIXES:
            for issue in find_issues(rel, ""):
                findings.append((rel, issue))
            continue
        if p.stat().st_size > 2_000_000:
            findings.append((rel, "oversized-text-not-checked"))
            continue
        contents = p.read_text(encoding="utf-8", errors="replace")
        scanned += 1
        for issue in find_issues(rel, contents):
            findings.append((rel, issue))
    for rel, issue in findings:
        print(f"BLOCK: {rel}: {issue}")
    print(f"Release hygiene: {scanned} text files scanned, {len(findings)} finding(s)")
    if findings:
        print("Review the local file privately; do not paste secrets in CI or PR comments.")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
