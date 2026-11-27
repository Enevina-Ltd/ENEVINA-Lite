import hashlib
import http.client
import http.server
import importlib.util
import ipaddress
from pathlib import Path
import tempfile
import threading
import unittest

spec=importlib.util.spec_from_file_location("ota_range_server",Path(__file__).resolve().parents[1]/"tools/cottage_ota_server.py")
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class RangeServerTests(unittest.TestCase):
    def test_resume_identity_and_legacy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            payload=bytes(range(256))*400
            path=root/"firmware.bin"
            path.write_bytes(payload)
            class Handler(mod.Handler):
                directory=root
                allowed_cidr=ipaddress.ip_network("127.0.0.0/8")
                allowed_clients=frozenset()
            server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
            worker=threading.Thread(target=server.serve_forever,daemon=True)
            worker.start()
            def get(headers):
                conn=http.client.HTTPConnection("127.0.0.1",server.server_port,timeout=3)
                try:
                    conn.request("GET","/firmware.bin",headers=headers)
                    response=conn.getresponse()
                    return response.status,dict(response.getheaders()),response.read()
                finally: conn.close()
            try:
                tag='"'+hashlib.sha256(payload).hexdigest()+'"'
                status,headers,data=get({})
                self.assertEqual((status,data),(200,payload))
                self.assertEqual(headers["ETag"],tag)
                start=0
                chunks=[]
                for end in (1234,32767,len(payload)-1):
                    status,headers,data=get({"Range":f"bytes={start}-{end}","If-Match":tag})
                    self.assertEqual(status,206)
                    self.assertEqual(headers["Content-Range"],f"bytes {start}-{end}/{len(payload)}")
                    self.assertEqual(int(headers["Content-Length"]),end-start+1)
                    chunks.append(data)
                    start=end+1
                self.assertEqual(b"".join(chunks),payload)
                self.assertEqual(get({"Range":"bytes=9-8","If-Match":tag})[0],416)
                self.assertEqual(get({"Range":"bytes=0-99999999","If-Match":tag})[0],416)
                self.assertEqual(get({"Range":"bytes=0-1,3-4","If-Match":tag})[0],416)
                path.write_bytes(b"x"*len(payload))
                self.assertEqual(get({"Range":"bytes=4-8","If-Match":tag})[0],412)
            finally:
                server.shutdown()
                server.server_close()
                worker.join(timeout=2)
