#!/usr/bin/env python3
"""Apply the Enevina measurement overlay to a clean, pinned CN105 checkout."""
import argparse
from pathlib import Path
import shutil
import subprocess

PIN = "6b997ec98bc37797909f9b50438c8bb480f7a631"
OVERLAY = Path(__file__).resolve().parents[1] / "integrations/cn105-esphome"


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    args = parser.parse_args()
    root = args.checkout.resolve()
    if git(root, "rev-parse", "HEAD") != PIN or git(root, "status", "--porcelain"):
        parser.error(f"expected a clean CN105 checkout at {PIN}")
    component = root / "components/cn105"
    files = ("enevina_verified_poll.h", "enevina_snapshot.cpp")
    if any((component / name).exists() for name in files):
        parser.error("overlay files already exist")
    patch = str(OVERLAY / "cn105-2026.5.2.patch")
    git(root, "apply", "--check", "--unidiff-zero", patch)
    git(root, "apply", "--unidiff-zero", patch)
    for name in files:
        shutil.copyfile(OVERLAY / name, component / name)


if __name__ == "__main__":
    main()
