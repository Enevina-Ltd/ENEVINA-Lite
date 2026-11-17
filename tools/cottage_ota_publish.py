#!/usr/bin/env python3
"""Stage a user-approved, P-256 signed OTA image over an SSH-authenticated local filesystem."""
import argparse,hashlib,os
from pathlib import Path
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cottage_ota_channel import channel_tag
MAGIC="ENEVINA_C24_OTA_V1"
MODEL="esp32s3-n16r8"
SIGNED_PULL_MARKER=b"ENEVINA_C24_SIGNED_PULL_OTA_CAPABLE_V1"
def is_signed_pull_capable_image(data, expected_channel):
    return (data.count(SIGNED_PULL_MARKER)==1
            and data.count(expected_channel.encode("ascii"))==1)
def canonical(version,size,digest):
    return (f"{MAGIC}\nmodel={MODEL}\nversion={version}\nsize={size}\nsha256={digest}\n").encode("ascii")
def previous_version(path):
    # Published AND quarantined signed releases count toward monotonically
    # increasing versions. A rejected test image must not free its number.
    latest=0
    for item in path.parent.glob("manifest*.txt"):
        if not item.is_file() or item.is_symlink():continue
        versions=[int(line[8:]) for line in
                  item.read_text("ascii").splitlines()
                  if line.startswith("version=")]
        if len(versions)!=1:
            raise RuntimeError("invalid archived manifest: "+item.name)
        latest=max(latest,versions[0])
    return latest
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--firmware",type=Path,required=True)
    p.add_argument("--version",type=int,required=True)
    p.add_argument("--root",type=Path,required=True)
    p.add_argument("--manifest-url",required=True)
    p.add_argument("--firmware-url",required=True)
    a=p.parse_args()
    if a.version<1 or a.version>2147483647:p.error("invalid version")
    data=a.firmware.read_bytes()
    if not 1024<=len(data)<=0x640000 or data[0]!=0xE9:p.error("invalid ESP32 image or partition size")
    try:
        expected_channel=channel_tag(
            a.manifest_url,a.firmware_url,
            (a.root/"ca-public.pem").read_text("ascii"),
            (a.root/"publisher-public.pem").read_text("ascii"))
    except (ValueError,OSError,UnicodeError) as exc:
        p.error("missing or invalid OTA channel public trust settings: "+str(exc))
    if not is_signed_pull_capable_image(data,expected_channel):
        p.error("OTA image signed-pull capability or trust/endpoint channel mismatch")
    folder=a.root/"releases"
    folder.mkdir(mode=0o700,parents=True,exist_ok=True)
    if a.version<=previous_version(folder/"manifest.txt"):p.error("non-increasing version")
    private=serialization.load_pem_private_key((a.root/"publisher-private.pem").read_bytes(),password=None)
    if not isinstance(private,ec.EllipticCurvePrivateKey) or not isinstance(private.curve,ec.SECP256R1):
        p.error("publisher signing key must be P256")
    payload=canonical(a.version,len(data),hashlib.sha256(data).hexdigest())
    sig=private.sign(payload,ec.ECDSA(hashes.SHA256()))
    manifest=payload+b"sig="+sig.hex().encode("ascii")+b"\n"
    if len(manifest)>512:p.error("oversized signed manifest")
    public=serialization.load_pem_public_key((a.root/"publisher-public.pem").read_bytes())
    public.verify(sig,payload,ec.ECDSA(hashes.SHA256()))
    image_tmp=folder/("fw-tmp-"+str(os.getpid()))
    manifest_tmp=folder/("mf-tmp-"+str(os.getpid()))
    try:
        with image_tmp.open("xb") as f:f.write(data);f.flush();os.fsync(f.fileno())
        with manifest_tmp.open("xb") as f:f.write(manifest);f.flush();os.fsync(f.fileno())
        os.replace(image_tmp,folder/"firmware.bin")
        os.replace(manifest_tmp,folder/"manifest.txt") # explicit approval to update
    finally:
        image_tmp.unlink(missing_ok=True);manifest_tmp.unlink(missing_ok=True)
    print(f"APPROVED_SIGNED_OTA_RELEASE version={a.version} bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}")
if __name__=="__main__":main()
