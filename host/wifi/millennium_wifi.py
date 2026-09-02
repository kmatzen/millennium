#!/usr/bin/env python3
"""Validated NetworkManager profiles and the Wi-Fi onboarding state machine."""

import json
import os
import re
import secrets
import socket
import subprocess
import tempfile
import time
import urllib.request
import uuid
from pathlib import Path

SETUP_CONNECTION = "millennium-setup"
OWNER_CONNECTION = "millennium-owner"
SETUP_ADDRESS = "10.42.0.1/24"
MAX_REQUEST = 8192


class WifiError(Exception):
    pass


def validate_device_id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9-]{1,32}", value):
        raise WifiError("invalid device ID")
    return value


def validate_ssid(value):
    if not isinstance(value, str):
        raise WifiError("SSID must be text")
    encoded = value.encode("utf-8")
    if not encoded or len(encoded) > 32 or any(byte < 32 or byte == 127 for byte in encoded):
        raise WifiError("SSID must contain 1..32 UTF-8 bytes without control characters")
    return value


def validate_security(value):
    if value not in ("wpa-psk", "open"):
        raise WifiError("unsupported network security")
    return value


def validate_passphrase(value, security):
    if not isinstance(value, str):
        raise WifiError("passphrase must be text")
    if security == "open":
        if value:
            raise WifiError("open networks do not use a passphrase")
        return value
    if re.fullmatch(r"[0-9A-Fa-f]{64}", value):
        return value
    if not 8 <= len(value) <= 63 or any(ord(char) < 32 or ord(char) > 126 for char in value):
        raise WifiError("WPA passphrase must be 8..63 printable ASCII characters")
    return value


def validate_request(request):
    if not isinstance(request, dict) or set(request) != {"ssid", "security", "passphrase", "hidden"}:
        raise WifiError("invalid connection request")
    ssid = validate_ssid(request["ssid"])
    security = validate_security(request["security"])
    passphrase = validate_passphrase(request["passphrase"], security)
    if not isinstance(request["hidden"], bool):
        raise WifiError("hidden must be true or false")
    return {"ssid": ssid, "security": security, "passphrase": passphrase,
            "hidden": request["hidden"]}


def keyfile_escape(value):
    return (value.replace("\\", "\\\\").replace(" ", "\\s")
            .replace("\n", "\\n").replace("\r", "\\r"))


def ssid_bytes(value):
    return ";".join(str(byte) for byte in value.encode("utf-8")) + ";"


def owner_keyfile(request):
    request = validate_request(request)
    lines = [
        "[connection]", "id=" + OWNER_CONNECTION,
        "uuid=" + str(uuid.uuid5(uuid.NAMESPACE_DNS, "millennium-owner")),
        "type=wifi", "autoconnect=true",
        "autoconnect-priority=100", "", "[wifi]", "mode=infrastructure",
        "ssid=" + ssid_bytes(request["ssid"]),
        "hidden=" + ("true" if request["hidden"] else "false"), "",
    ]
    if request["security"] == "wpa-psk":
        lines.extend(("[wifi-security]", "key-mgmt=wpa-psk", "psk=" + keyfile_escape(request["passphrase"]), ""))
    lines.extend(("[ipv4]", "method=auto", "", "[ipv6]", "method=auto", ""))
    return "\n".join(lines)


def setup_keyfile(device_id, passphrase):
    ssid = "Millennium-Setup-" + validate_device_id(device_id)
    validate_passphrase(passphrase, "wpa-psk")
    return "\n".join((
        "[connection]", "id=" + SETUP_CONNECTION,
        "uuid=" + str(uuid.uuid5(uuid.NAMESPACE_DNS, "millennium-setup-" + device_id)),
        "type=wifi", "autoconnect=false", "",
        "[wifi]", "mode=ap", "band=bg", "channel=6", "ssid=" + ssid_bytes(ssid), "",
        "[wifi-security]", "key-mgmt=wpa-psk", "psk=" + keyfile_escape(passphrase), "",
        "[ipv4]", "method=shared", "address1=" + SETUP_ADDRESS, "",
        "[ipv6]", "method=disabled", ""
    ))


def write_secret(path, content):
    path = Path(path)
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=str(path.parent))
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


class NetworkManager:
    def __init__(self, run=subprocess.run, profile_dir="/etc/NetworkManager/system-connections"):
        self.run = run
        self.profile_dir = Path(profile_dir)

    def command(self, *arguments, check=True):
        return self.run(["/usr/bin/nmcli", *arguments], check=check, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)

    def load_profile(self, name, content):
        path = self.profile_dir / (name + ".nmconnection")
        write_secret(path, content)
        self.command("connection", "load", str(path))

    def start_setup(self, device_id, passphrase):
        self.load_profile(SETUP_CONNECTION, setup_keyfile(device_id, passphrase))
        self.command("connection", "up", SETUP_CONNECTION)

    def scan(self):
        result = self.command("--terse", "--escape", "yes", "--fields", "SSID,SECURITY,SIGNAL",
                              "device", "wifi", "list", "--rescan", "yes")
        networks = []
        seen = set()
        for line in result.stdout.splitlines():
            fields = re.split(r"(?<!\\):", line)
            if len(fields) != 3:
                continue
            ssid = fields[0].replace("\\:", ":").replace("\\\\", "\\")
            if not ssid or ssid in seen:
                continue
            try:
                validate_ssid(ssid)
                signal = max(0, min(100, int(fields[2])))
            except (WifiError, ValueError):
                continue
            seen.add(ssid)
            networks.append({"ssid": ssid, "security": "open" if fields[1] == "--" else "wpa-psk",
                             "signal": signal})
        return sorted(networks, key=lambda item: (-item["signal"], item["ssid"]))

    def apply_owner(self, request):
        path = self.profile_dir / (OWNER_CONNECTION + ".nmconnection")
        backup = self.profile_dir / (OWNER_CONNECTION + ".last-good")
        if path.exists():
            write_secret(backup, path.read_text(encoding="utf-8"))
        self.load_profile(OWNER_CONNECTION, owner_keyfile(request))
        self.command("connection", "down", SETUP_CONNECTION, check=False)
        result = self.command("connection", "up", OWNER_CONNECTION, check=False)
        return result.returncode == 0

    def restore_owner(self):
        path = self.profile_dir / (OWNER_CONNECTION + ".nmconnection")
        backup = self.profile_dir / (OWNER_CONNECTION + ".last-good")
        if backup.exists():
            os.replace(backup, path)
            os.chmod(path, 0o600)
            self.command("connection", "load", str(path), check=False)
        else:
            try:
                path.unlink()
            except FileNotFoundError:
                pass
            self.command("connection", "delete", OWNER_CONNECTION, check=False)

    def restore_setup(self):
        self.command("connection", "up", SETUP_CONNECTION, check=False)


def connectivity_ok(url="https://updates.kmatzen.com/millennium/stable/manifest.json"):
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "millennium-wifi-setup/1"})
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status == 200 and 0 < int(response.headers.get("Content-Length", "0")) <= 65536
    except (OSError, ValueError):
        return False


def read_json_line(stream):
    line = stream.readline(MAX_REQUEST + 1)
    if not line or len(line) > MAX_REQUEST or not line.endswith(b"\n"):
        raise WifiError("invalid request size")
    try:
        return json.loads(line.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise WifiError("invalid JSON") from exc


def generate_setup_password():
    alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
    raw = "".join(secrets.choice(alphabet) for unused in range(16))
    return "-".join(raw[index:index + 4] for index in range(0, 16, 4))
