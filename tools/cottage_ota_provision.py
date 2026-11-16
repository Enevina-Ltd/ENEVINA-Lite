#!/usr/bin/env python3
"""One-time private bootstrap for Enevina Lite C2.4 LAN signed OTA.
No secrets or private IP configuration belong in the Git repository.
"""
from datetime import datetime, timedelta, timezone
from pathlib import Path
import ipaddress, os, argparse
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

parser=argparse.ArgumentParser()
parser.add_argument("--root",required=True,type=Path)
parser.add_argument("--lan-ip",required=True)
args=parser.parse_args()
ROOT=args.root
IP=ipaddress.IPv4Address(args.lan_ip)
ROOT.mkdir(mode=0o700,parents=True,exist_ok=True)
os.chmod(ROOT,0o700)
def save(path,data,mode=0o600):
    if path.exists(): raise SystemExit("Refusing overwrite of "+str(path))
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,mode)
    with os.fdopen(fd,"wb") as h:h.write(data)
now=datetime.now(timezone.utc)
ca_key=ec.generate_private_key(ec.SECP256R1())
ca_name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,"Enevina Lite Local OTA Root")])
ca_cert=(x509.CertificateBuilder().subject_name(ca_name).issuer_name(ca_name)
    .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
    .not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=3650))
    .add_extension(x509.BasicConstraints(ca=True,path_length=0),critical=True)
    .add_extension(x509.KeyUsage(digital_signature=True,key_encipherment=False,
    key_cert_sign=True,crl_sign=True,key_agreement=True,data_encipherment=False,
    content_commitment=False,encipher_only=False,decipher_only=False),critical=True)
    .sign(ca_key,hashes.SHA256()))
sv_key=ec.generate_private_key(ec.SECP256R1())
sv_name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,str(IP))])
sv_cert=(x509.CertificateBuilder().subject_name(sv_name).issuer_name(ca_name)
    .public_key(sv_key.public_key()).serial_number(x509.random_serial_number())
    .not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=365))
    .add_extension(x509.BasicConstraints(ca=False,path_length=None),critical=True)
    .add_extension(x509.SubjectAlternativeName([x509.DNSName(str(IP)),x509.IPAddress(IP)]),critical=False)
    .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),critical=False)
    .add_extension(x509.KeyUsage(digital_signature=True,key_encipherment=False,
    key_cert_sign=False,crl_sign=False,key_agreement=True,data_encipherment=False,
    content_commitment=False,encipher_only=False,decipher_only=False),critical=True)
    .sign(ca_key,hashes.SHA256()))
publisher_key=ec.generate_private_key(ec.SECP256R1())
private=lambda key: key.private_bytes(serialization.Encoding.PEM,
    serialization.PrivateFormat.PKCS8,serialization.NoEncryption())
save(ROOT/"ca-private.pem",private(ca_key))
save(ROOT/"ca-public.pem",ca_cert.public_bytes(serialization.Encoding.PEM),0o644)
save(ROOT/"server-private.pem",private(sv_key))
save(ROOT/"server-cert.pem",sv_cert.public_bytes(serialization.Encoding.PEM),0o644)
save(ROOT/"publisher-private.pem",private(publisher_key))
save(ROOT/"publisher-public.pem",publisher_key.public_key().public_bytes(
    serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo),0o644)
(ROOT/"releases").mkdir(mode=0o700,exist_ok=True)
print("PRIVATE_OTA_KEYS_AND_SAN_CERT_CREATED; private keys never leave device filesystem")
print("SERVER_IP_SAN_VERIFIED",IP in sv_cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value.get_values_for_type(x509.IPAddress))
