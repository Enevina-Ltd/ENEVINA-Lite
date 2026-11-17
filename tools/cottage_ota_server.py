#!/usr/bin/env python3
"""Read-only HTTPS OTA distribution. No POST, PUT, upload or credential endpoint."""
import argparse,ssl,ipaddress,http.server
from pathlib import Path
class Handler(http.server.BaseHTTPRequestHandler):
    directory=Path(".")
    def do_GET(self):
        paths={"/manifest.txt":"manifest.txt","/firmware.bin":"firmware.bin"}
        name=paths.get(self.path)
        if not name: self.send_error(404);return
        try:
            if ipaddress.ip_address(self.client_address[0]) not in self.allowed_cidr:
                self.send_error(403);return
        except ValueError: self.send_error(403);return
        path=self.directory/name
        if not path.is_file() or path.is_symlink(): self.send_error(404);return
        size=path.stat().st_size
        if size<1 or size>(512 if name=="manifest.txt" else 0x640000):
            self.send_error(500);return
        try:
            with path.open("rb") as body:
                self.send_response(200)
                self.send_header("Content-Length",str(size))
                self.send_header("Content-Type","text/plain" if name=="manifest.txt" else "application/octet-stream")
                self.send_header("Cache-Control","no-store")
                self.end_headers()
                while chunk:=body.read(8192): self.wfile.write(chunk)
        except (OSError,ConnectionResetError,BrokenPipeError):
            self.close_connection=True
    def do_POST(self): self.send_error(405)
    def do_PUT(self): self.send_error(405)
    def do_DELETE(self): self.send_error(405)
    def log_message(self,format,*args):pass

class ReadOnlyTLSServer(http.server.ThreadingHTTPServer):
    # Accept TCP immediately; perform the TLS handshake in its own request
    # thread. SSL-wrapping the LISTENING socket instead blocks accept() on an
    # idle client and prevents unrelated OTA downloads indefinitely.
    daemon_threads=True
    def __init__(self,address,handler,tls_context):
        self.tls_context=tls_context
        super().__init__(address,handler)
    def finish_request(self,request,client_address):
        # Called from ThreadingMixIn.process_request_thread, not accept loop.
        try:
            request.settimeout(5.0)
            with self.tls_context.wrap_socket(
                    request,server_side=True,do_handshake_on_connect=False) as tls:
                tls.settimeout(5.0)
                tls.do_handshake()
                super().finish_request(tls,client_address)
        except (OSError,ssl.SSLError):
            # A stalled, aborted, or non-TLS connection must not crash or
            # monopolize the read-only OTA distribution service.
            return

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--bind",required=True)
    p.add_argument("--lan-cidr",required=True)
    p.add_argument("--port",type=int,default=8443)
    p.add_argument("--root",type=Path,required=True)
    args=p.parse_args()
    Handler.allowed_cidr=ipaddress.ip_network(args.lan_cidr)
    assert ipaddress.ip_address(args.bind) in Handler.allowed_cidr
    Handler.directory=args.root/"releases"
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version=ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(args.root/"server-cert.pem"),str(args.root/"server-private.pem"))
    server=ReadOnlyTLSServer((args.bind,args.port),Handler,context)
    print("OTA_HTTPS_READONLY_SERVER_READY",flush=True)
    server.serve_forever()
if __name__=="__main__":main()
