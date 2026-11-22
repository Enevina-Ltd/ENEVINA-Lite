"""Canonical, public OTA channel binding shared by firmware generation/publishing."""
import hashlib
import re
from urllib.parse import urlsplit


def channel_directory(parsed, filename):
    # Static hosts can serve project sites below a path prefix. Keep paths
    # literal and canonical before binding the exact URL in the marker.
    pattern = r"/(?:[A-Za-z0-9][A-Za-z0-9._-]*/)*" + re.escape(filename)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username
            or parsed.password or parsed.query or parsed.fragment
            or not re.fullmatch(pattern, parsed.path)):
        raise ValueError("signed OTA requires canonical HTTPS endpoints")
    return parsed.path.rsplit("/", 1)[0]


def channel_tag(manifest_url, firmware_url, ca_pem, publisher_pem):
    """Bind exact HTTPS endpoints, CA certificate, and publisher public key.

    The marker prevents accidental signing for a different installed channel;
    it is not cryptographic attestation of device-side execution.
    """
    if any(
        not url.isascii() or any(char in url for char in "?#")
        or any(ord(char) <= 32 or ord(char) == 127 for char in url)
        for url in (manifest_url, firmware_url)
    ):
        raise ValueError("signed OTA requires canonical HTTPS endpoints")
    a = urlsplit(manifest_url)
    b = urlsplit(firmware_url)
    a_dir = channel_directory(a, "manifest.txt")
    b_dir = channel_directory(b, "firmware.bin")
    if a.netloc != b.netloc or a_dir != b_dir:
        raise ValueError("manifest and firmware endpoints must share authority and directory")
    parts = [
        b"ENEVINA_C24_OTA_CHANNEL_V1",
        manifest_url.encode("utf-8"),
        firmware_url.encode("utf-8"),
        (ca_pem.strip() + "\n").encode("ascii"),
        (publisher_pem.strip() + "\n").encode("ascii"),
    ]
    return "ENEVINA_C24_CHANNEL_" + hashlib.sha256(b"\x00".join(parts)).hexdigest()
