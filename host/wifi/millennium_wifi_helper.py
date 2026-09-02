#!/usr/bin/env python3
"""Root-only NetworkManager broker for the unprivileged setup portal."""

import argparse
import json
import os
import socket
import subprocess
import threading
import time
from pathlib import Path

from millennium_wifi import NetworkManager, WifiError, connectivity_ok, read_json_line, validate_request


class Helper:
    def __init__(self, manager, state_dir):
        self.manager = manager
        self.state_dir = Path(state_dir)
        self.lock = threading.Lock()

    def connect(self, request):
        request = validate_request(request)
        if not self.lock.acquire(blocking=False):
            raise WifiError("a connection attempt is already running")
        threading.Thread(target=self._apply, args=(request,), daemon=True).start()

    def _apply(self, request):
        status = self.state_dir / "status.json"
        try:
            self.state_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
            status.write_text(json.dumps({"state": "connecting"}), encoding="utf-8")
            os.chmod(status, 0o644)
            if not self.manager.apply_owner(request):
                raise WifiError("the network rejected the connection")
            success = False
            for unused in range(12):
                if connectivity_ok():
                    success = True
                    break
                time.sleep(5)
            if not success:
                raise WifiError("connected locally but could not reach the update service")
            (self.state_dir / "owner-network-configured").touch(mode=0o600)
            subprocess.run(["/usr/bin/systemctl", "try-restart",
                            "millennium-maintenance-tunnel.service"], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["/usr/bin/systemctl", "start",
                            "millennium-update-check.service"], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            status.write_text(json.dumps({"state": "connected"}), encoding="utf-8")
            os.chmod(status, 0o644)
        except Exception as exc:
            status.write_text(json.dumps({"state": "failed", "message": str(exc)}), encoding="utf-8")
            os.chmod(status, 0o644)
            self.manager.restore_owner()
            self.manager.restore_setup()
        finally:
            self.lock.release()

    def dispatch(self, message):
        if not isinstance(message, dict) or "action" not in message:
            raise WifiError("missing action")
        if message["action"] == "scan" and set(message) == {"action"}:
            return {"ok": True, "networks": self.manager.scan()}
        if message["action"] == "connect" and set(message) == {"action", "network"}:
            self.connect(message["network"])
            return {"ok": True, "accepted": True}
        if message["action"] == "status" and set(message) == {"action"}:
            path = self.state_dir / "status.json"
            return {"ok": True, **(json.loads(path.read_text()) if path.exists() else {"state": "setup"})}
        raise WifiError("unsupported action")


def serve(socket_path, helper):
    path = Path(socket_path)
    path.parent.mkdir(mode=0o750, parents=True, exist_ok=True)
    try:
        path.unlink()
    except FileNotFoundError:
        pass
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(path))
    os.chmod(path, 0o660)
    server.listen(8)
    while True:
        connection, unused = server.accept()
        with connection, connection.makefile("rwb", buffering=0) as stream:
            try:
                response = helper.dispatch(read_json_line(stream))
            except WifiError as exc:
                response = {"ok": False, "error": str(exc)}
            except Exception:
                response = {"ok": False, "error": "internal setup error"}
            stream.write(json.dumps(response, separators=(",", ":")).encode() + b"\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", default="/run/millennium-wifi/helper.sock")
    parser.add_argument("--state-dir", default="/var/lib/millennium/wifi")
    args = parser.parse_args()
    serve(args.socket, Helper(NetworkManager(), args.state_dir))


if __name__ == "__main__":
    main()
