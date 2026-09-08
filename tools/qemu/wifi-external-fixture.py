#!/usr/bin/env python3
"""Run the real Wi-Fi portal behind a QEMU-reachable test boundary."""

import json
import os
import selectors
import socket
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from host.wifi import millennium_wifi_portal as portal

STATE = Path("/run/millennium-wifi-external")
HELPER = STATE / "helper.sock"
CAPTURE = STATE / "connect.json"


def helper():
    HELPER.unlink(missing_ok=True)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(HELPER))
    server.listen()
    while True:
        connection, _ = server.accept()
        with connection:
            request = json.loads(connection.makefile("rb").readline())
            if request.get("action") == "scan":
                response = {"ok": True, "networks": [{"ssid": "QEMU Owner Network"}]}
            elif request.get("action") == "connect":
                CAPTURE.write_text(json.dumps(request, sort_keys=True) + "\n")
                os.chmod(CAPTURE, 0o600)
                response = {"ok": True}
            else:
                response = {"ok": False, "error": "unsupported action"}
            connection.sendall((json.dumps(response) + "\n").encode())


def relay(left, right):
    selector = selectors.DefaultSelector()
    selector.register(left, selectors.EVENT_READ, right)
    selector.register(right, selectors.EVENT_READ, left)
    try:
        while True:
            for key, _ in selector.select():
                data = key.fileobj.recv(65536)
                if not data:
                    return
                key.data.sendall(data)
    finally:
        left.close()
        right.close()


def proxy():
    listener = socket.create_server(("0.0.0.0", 18082))
    while True:
        outside, _ = listener.accept()
        inside = socket.create_connection(("10.42.0.1", 80))
        threading.Thread(target=relay, args=(outside, inside), daemon=True).start()


def main():
    STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
    CAPTURE.unlink(missing_ok=True)
    portal.SOCKET_PATH = str(HELPER)
    threading.Thread(target=helper, daemon=True).start()
    threading.Thread(target=proxy, daemon=True).start()
    server = portal.ThreadingHTTPServer(("10.42.0.1", 80), portal.Portal)
    (STATE / "ready").write_text("ready\n")
    server.serve_forever()


if __name__ == "__main__":
    main()
