"""Canonical, public OTA channel binding shared by firmware generation/publishing."""
import hashlib
from urllib.parse import urlsplit


def channel_tag(manifest_url, firmware_url, ca_pem, publisher_pem):
    """Bind exact HTTPS endpoints, CA certificate, and publisher public key.

    The marker prevents accidental signing for a different installed channel;
    it is not cryptographic attestation of device-side execution.
    """
    a = urlsplit(manifest_url)
    b = urlsplit(firmware_url)
    for parsed, path in ((a, "/manifest.txt"), (b, "/firmware.bin")):
        if (parsed.scheme != "https" or not parsed.hostname
                or parsed.username or parsed.password or parsed.query
                or parsed.fragment or parsed.path != path):
            raise ValueError("signed OTA requires canonical HTTPS endpoints")
    if a.netloc != b.netloc:
        raise ValueError("manifest and firmware endpoints must share authority")
    parts = [
        b"ENEVINA_C24_OTA_CHANNEL_V1",
        manifest_url.encode("utf-8"),
        firmware_url.encode("utf-8"),
        (ca_pem.strip() + "\n").encode("ascii"),
        (publisher_pem.strip() + "\n").encode("ascii"),
    ]
    return "ENEVINA_C24_CHANNEL_" + hashlib.sha256(b"\x00".join(parts)).hexdigest()
