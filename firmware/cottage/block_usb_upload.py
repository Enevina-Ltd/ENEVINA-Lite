#!/usr/bin/env python3
"""Fail closed: a standard PlatformIO upload replaces the validated bootloader.

Do not turn this into an automatic flasher. An explicitly approved separate
app-only procedure must verify the partition table and rollback bootloader.
"""
import sys

sys.stderr.write(
    "ENEVINA_LITE_USB_UPLOAD_BLOCKED: standard PlatformIO upload may "
    "replace the rollback bootloader and otadata; use a separately "
    "reviewed and approved app-only flashing procedure.\n"
)
raise SystemExit(5)
