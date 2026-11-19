#!/usr/bin/env python3
"""One-off LOCAL dev pairing: copy Core's already-working Midea V3 credentials to Lite.

No credentials, tokens, IPs or raw replies are logged. No pump commands.
Only use on a personally trusted local network: Lite's development HTTP setup
is not encrypted. This tool is not a production onboarding solution.
"""
import argparse
import http.client
import ipaddress
import json
import re
from pathlib import Path
from urllib.parse import urlencode

DEFAULT_CONFIG = Path("/opt/kaelion/config/config.yaml")


def private_ip(s: str) -> str:
    ip = ipaddress.IPv4Address(s)
    supported = (
        ip in ipaddress.IPv4Network("10.0.0.0/8")
        or ip in ipaddress.IPv4Network("172.16.0.0/12")
        or ip in ipaddress.IPv4Network("192.168.0.0/16")
    )
    if not supported:
        raise ValueError("Only RFC1918 private IPv4 LAN addresses are supported")
    return str(ip)


def core_midea_config(path: Path) -> dict[str, str]:
    import yaml

    obj = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    m = obj.get("midea_lan") or {}
    if not isinstance(m, dict):
        raise ValueError("Invalid Core Midea config")
    host = private_ip(str(m.get("host", "")).strip())
    if int(m.get("port", 6444)) != 6444:
        raise ValueError("Core Midea port is not 6444; Lite client requires 6444")
    device_id = str(m.get("device_id", ""))
    token = str(m.get("token", "")).strip()
    key = str(m.get("key", "")).strip()
    if not re.fullmatch(r"[0-9]{1,20}", device_id) or int(device_id) == 0:
        raise ValueError("Core config does not contain a valid Midea device ID")
    if not re.fullmatch(r"[0-9a-fA-F]{128}", token):
        raise ValueError("Core config does not contain a 128-hex Midea V3 token")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", key):
        raise ValueError("Core config does not contain a 64-hex Midea V3 key")
    return {"host": host, "device_id": device_id, "v3_token": token, "v3_key": key}


def local_request(host: str, method: str, path: str, fields=None) -> dict:
    # http.client intentionally DOES NOT follow redirects, so a captive
    # portal cannot redirect the pairing secrets to a different host.
    conn = http.client.HTTPConnection(host, port=80, timeout=8)
    try:
        body = urlencode(fields).encode("ascii") if fields is not None else None
        headers = {"Content-Type": "application/x-www-form-urlencoded"} if body else {}
        conn.request(method, path, body=body, headers=headers)
        resp = conn.getresponse()
        if resp.status not in (200, 202):
            # Do not echo response bodies, which could contain server data.
            raise RuntimeError(f"Lite returned HTTP {resp.status} for {path}")
        blob = resp.read(16384)
        if len(blob) >= 16384:
            raise RuntimeError("Lite response too large")
        return json.loads(blob)
    finally:
        conn.close()


def pair(lite_host: str, config: dict[str, str]) -> None:
    device = local_request(lite_host, "GET", "/api/midea/status")
    if device.get("selected") is not True:
        raise RuntimeError("Select the Midea LAN adapter in Lite settings first")
    status = local_request(lite_host, "GET", "/api/status")
    token = ((status.get("air") or {}).get("config_token") or "")
    if not isinstance(token, str) or not token:
        raise RuntimeError("Lite has no current local settings token")
    response = local_request(lite_host, "POST", "/api/midea/config", {**config, "token": token})
    if response.get("saved") is not True:
        raise RuntimeError("Lite did not confirm pairing")
    print("Midea V3 credentials stored in Lite; no secrets printed.")
    print("Open Lite Settings > Testaa yhteys nyt; no physical commands are enabled.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lite-ip", help="ESP32 Lite's private LAN IP after Wi-Fi onboarding")
    parser.add_argument("--core-config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--check-only", action="store_true", help="Validate Core credentials locally; no network traffic")
    args = parser.parse_args()
    try:
        config = core_midea_config(args.core_config)
        if args.check_only:
            print("Core Midea V3 credentials format OK. No network calls or changes.")
            return
        if not args.lite_ip:
            parser.error("--lite-ip required unless --check-only")
        pair(private_ip(args.lite_ip), config)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        # Errors are sanitized; never print config values.
        raise SystemExit(f"Pairing failed: {type(exc).__name__}: {exc}") from None


if __name__ == "__main__":
    main()