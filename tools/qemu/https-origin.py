#!/usr/bin/env python3
"""Minimal HTTPS file origin used only inside the QEMU appliance."""

import argparse
import http.server
import os
import ssl
from urllib.parse import urlsplit


class FaultHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *values, directory, fail_path=None, cutoff_path=None,
                 cutoff_bytes=1024, **kwargs):
        self.fail_path = fail_path
        self.cutoff_path = cutoff_path
        self.cutoff_bytes = cutoff_bytes
        super().__init__(*values, directory=directory, **kwargs)

    def do_GET(self):
        path = urlsplit(self.path).path
        if self.fail_path and path == self.fail_path:
            self.send_error(503, "injected origin failure")
            return
        if self.cutoff_path and path == self.cutoff_path:
            file_path = self.translate_path(path)
            try:
                size = os.path.getsize(file_path)
                source = open(file_path, "rb")
            except OSError:
                self.send_error(404)
                return
            with source:
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(size))
                self.end_headers()
                self.wfile.write(source.read(min(size, self.cutoff_bytes)))
                self.wfile.flush()
                self.close_connection = True
            return
        super().do_GET()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", required=True)
    parser.add_argument("--certificate", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--fail-path")
    parser.add_argument("--cutoff-path")
    parser.add_argument("--cutoff-bytes", type=int, default=1024)
    args = parser.parse_args()
    handler = lambda *values, **kwargs: FaultHandler(
        *values, directory=args.directory, fail_path=args.fail_path,
        cutoff_path=args.cutoff_path, cutoff_bytes=args.cutoff_bytes, **kwargs)
    server = http.server.ThreadingHTTPServer((args.bind, args.port), handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.certificate, args.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
