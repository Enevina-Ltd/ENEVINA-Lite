"""P2: an idle TCP client must not block another TLS OTA download."""
import importlib.util
import ipaddress
import pathlib
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest

SCRIPT=pathlib.Path(__file__).resolve().parents[1]/"tools/cottage_ota_server.py"
spec=importlib.util.spec_from_file_location("cottage_ota_server_test",SCRIPT)
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

@unittest.skipUnless(shutil.which("openssl"),"openssl required for ephemeral test certificate")
class OTAHandshakeTests(unittest.TestCase):
    def test_idle_raw_connection_does_not_block_valid_tls(self):
        self.assertTrue(hasattr(mod,"ReadOnlyTLSServer"),"use threaded per-client TLS handshakes")
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp)
            (root/"releases").mkdir()
            (root/"releases"/"manifest.txt").write_text("firmware_ok\\n")
            cert=root/"cert.pem"
            key=root/"key.pem"
            subprocess.run(["openssl","req","-x509","-nodes","-newkey","rsa:2048",
                "-keyout",str(key),"-out",str(cert),"-days","1",
                "-subj","/CN=localhost"],check=True,stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL)
            ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.minimum_version=ssl.TLSVersion.TLSv1_2
            ctx.load_cert_chain(str(cert),str(key))
            mod.Handler.allowed_cidr=ipaddress.ip_network("127.0.0.0/8")
            mod.Handler.allowed_clients=frozenset()
            mod.Handler.directory=root/"releases"
            server=mod.ReadOnlyTLSServer(("127.0.0.1",0),mod.Handler,ctx)
            t=threading.Thread(target=server.serve_forever,daemon=True)
            t.start()
            idle=None
            try:
                addr=("127.0.0.1",server.server_port)
                idle=socket.create_connection(addr,timeout=2)
                time.sleep(0.08)
                client=ssl.create_default_context()
                client.check_hostname=False
                client.verify_mode=ssl.CERT_NONE
                start=time.monotonic()
                with client.wrap_socket(socket.create_connection(addr,timeout=3),
                                        server_hostname="localhost") as conn:
                    conn.settimeout(3)
                    conn.sendall(b"GET /manifest.txt HTTP/1.0\r\nHost: localhost\r\n\r\n")
                    payload=conn.recv(512)
                self.assertIn(b"200 OK",payload)
                self.assertLess(time.monotonic()-start,2.0)
                # A separately connected Lite device must not receive a
                # signed trial release intended for just one ESP32.
                mod.Handler.allowed_clients=frozenset({ipaddress.ip_address("127.0.0.2")})
                with client.wrap_socket(socket.create_connection(addr,timeout=3),
                                        server_hostname="localhost") as conn:
                    conn.settimeout(3)
                    conn.sendall(b"GET /manifest.txt HTTP/1.0\r\nHost: localhost\r\n\r\n")
                    rejected=conn.recv(512)
                self.assertIn(b"403 Forbidden",rejected)
            finally:
                if idle: idle.close()
                server.shutdown()
                server.server_close()
                t.join(timeout=2)

if __name__=="__main__":
    unittest.main()
