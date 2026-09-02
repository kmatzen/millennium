#!/usr/bin/env python3
"""Minimal HTTPS file origin used only inside the QEMU appliance."""

import argparse
import http.server
import ssl


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", required=True)
    parser.add_argument("--certificate", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()
    handler = lambda *values, **kwargs: http.server.SimpleHTTPRequestHandler(
        *values, directory=args.directory, **kwargs)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.certificate, args.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
