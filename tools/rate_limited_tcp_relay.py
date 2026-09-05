#!/usr/bin/env python3
"""One-shot TCP relay with a paced server-to-client stream and JSON evidence."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import socket
import tempfile
import threading
import time


def now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".tcp-relay-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def copy_requests(client, upstream, report, output):
    try:
        while True:
            chunk = client.recv(16384)
            if not chunk:
                try:
                    upstream.shutdown(socket.SHUT_WR)
                except OSError:
                    pass
                return
            upstream.sendall(chunk)
            report["request_bytes"] += len(chunk)
            atomic_json(output, report)
    except OSError as error:
        if error.errno != 9:  # listener closed after a completed response
            report["request_error"] = str(error)
            atomic_json(output, report)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen-address", default="192.168.8.239")
    parser.add_argument("--listen-port", type=int, default=8444)
    parser.add_argument("--upstream-address", default="127.0.0.1")
    parser.add_argument("--upstream-port", type=int, default=8443)
    parser.add_argument("--rate", type=int, default=16384,
                        help="maximum response bytes per second")
    parser.add_argument("--connections", type=int, default=3,
                        help="number of sequential TLS connections to relay")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    report = {"schema": 1, "operation": "rate-limited-tcp-relay",
              "started_at": now(), "status": "listening", "request_bytes": 0,
              "response_bytes": 0, "rate_bytes_per_second": args.rate}
    atomic_json(args.output, report)
    try:
        with socket.create_server((args.listen_address, args.listen_port)) as listener:
            for sequence in range(1, args.connections + 1):
                client, peer = listener.accept()
                with client, socket.create_connection(
                        (args.upstream_address, args.upstream_port), timeout=15) as upstream:
                    client.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8192)
                    report.update({"status": "streaming", "accepted_at": now(),
                                   "connection": sequence, "peer": peer[0]})
                    atomic_json(args.output, report)
                    requests = threading.Thread(target=copy_requests,
                                                args=(client, upstream, report, args.output),
                                                daemon=True)
                    requests.start()
                    while True:
                        chunk = upstream.recv(4096)
                        if not chunk:
                            break
                        time.sleep(len(chunk) / args.rate)
                        client.sendall(chunk)
                        report["response_bytes"] += len(chunk)
                        atomic_json(args.output, report)
                if sequence < args.connections:
                    report["status"] = "listening"
                    atomic_json(args.output, report)
            report["status"] = "complete"
    except Exception as error:
        report.update({"status": "interrupted", "error": str(error)})
        atomic_json(args.output, report)
        print(json.dumps(report, sort_keys=True))
        return 1
    report["completed_at"] = now()
    atomic_json(args.output, report)
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
