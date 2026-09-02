#!/usr/bin/env python3
"""Factory-generate the per-device setup secret and printable handoff data."""

import argparse
import json
from pathlib import Path

from millennium_wifi import generate_setup_password, validate_device_id, write_secret


def wifi_qr(ssid, password):
    escape = lambda value: value.replace("\\", "\\\\").replace(";", "\\;").replace(",", "\\,").replace(":", "\\:")
    return "WIFI:T:WPA;S:%s;P:%s;;" % (escape(ssid), escape(password))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--output-dir", default="/etc/millennium")
    parser.add_argument("--handoff", required=True)
    parser.add_argument("--force", action="store_true",
                        help="rotate an existing setup identity and password")
    args = parser.parse_args()
    device_id = validate_device_id(args.device_id)
    password = generate_setup_password()
    output = Path(args.output_dir)
    targets = (output / "device-id", output / "wifi-setup-password", Path(args.handoff))
    if not args.force and any(path.exists() for path in targets):
        raise SystemExit("refusing to overwrite existing provisioning data; use --force to rotate")
    write_secret(output / "device-id", device_id + "\n")
    write_secret(output / "wifi-setup-password", password + "\n")
    ssid = "Millennium-Setup-" + device_id
    handoff = {"device_id": device_id, "ssid": ssid, "password": password,
               "portal": "http://setup.millennium/", "wifi_qr": wifi_qr(ssid, password)}
    write_secret(args.handoff, json.dumps(handoff, indent=2) + "\n")
    print("Provisioned %s; handoff data written to %s" % (device_id, args.handoff))


if __name__ == "__main__":
    main()
