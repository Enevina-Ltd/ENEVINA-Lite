#!/usr/bin/env python3
"""Generate an ESP32-only public trust-anchor config for local HTTPS OTA.
The device never receives the CA private key or firmware signing private key.
"""
import argparse, json, os
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography import x509
from cottage_ota_channel import channel_tag

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--root",type=Path,required=True)
    p.add_argument("--manifest-url",required=True)
    p.add_argument("--firmware-url",required=True)
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args()
    ca=(a.root/"ca-public.pem").read_text(encoding="ascii").strip()+"\n"
    key=(a.root/"publisher-public.pem").read_text(encoding="ascii").strip()+"\n"
    x509.load_pem_x509_certificate(ca.encode("ascii"))
    public=serialization.load_pem_public_key(key.encode("ascii"))
    if not isinstance(public,ec.EllipticCurvePublicKey) or not isinstance(public.curve,ec.SECP256R1):
        p.error("Firmware publisher public key must be ECDSA P-256")
    if a.output.exists():
        p.error("Refusing to overwrite local trust anchors without explicit review")
    try:
        marker=channel_tag(a.manifest_url,a.firmware_url,ca,key)
    except ValueError as exc:
        p.error(str(exc))
    content=(
        "#pragma once\n"
        "// Generated private board configuration: trust anchors are public keys only.\n"
        "#define COTTAGE_OTA_MANIFEST_URL "+json.dumps(a.manifest_url)+"\n"
        "#define COTTAGE_OTA_FIRMWARE_URL "+json.dumps(a.firmware_url)+"\n"
        "#define COTTAGE_OTA_CHANNEL_MARKER "+json.dumps(marker)+"\n"
        'inline constexpr char COTTAGE_OTA_LOCAL_ROOT_CA[] = R"OTACA('+ca+')OTACA";\n'
        'inline constexpr char COTTAGE_OTA_PUBLISHER_PUBLIC_KEY[] = R"OTAPUB('+key+')OTAPUB";\n'
    )
    a.output.parent.mkdir(parents=True,exist_ok=True)
    fd=os.open(a.output,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
    with os.fdopen(fd,"w",encoding="ascii") as target: target.write(content)
    print("PUBLIC_OTA_TRUST_CONFIG_CREATED; PRIVATE_KEYS_NEVER_IN_FIRMWARE")

if __name__=="__main__": main()
