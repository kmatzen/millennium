#!/usr/bin/env python3
"""Start setup mode only for an unconfigured phone or explicit owner request."""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

from millennium_wifi import NetworkManager, WifiError, validate_device_id


def read_value(path, maximum=128):
    value = Path(path).read_text(encoding="utf-8").strip()
    if not value or len(value) > maximum:
        raise WifiError("invalid provisioning value")
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-dir", default="/var/lib/millennium/wifi")
    parser.add_argument("--device-id", default="/etc/millennium/device-id")
    parser.add_argument("--setup-password", default="/etc/millennium/wifi-setup-password")
    args = parser.parse_args()
    state = Path(args.state_dir)
    configured = state / "owner-network-configured"
    requested = state / "setup-requested"
    if configured.exists() and not requested.exists():
        return 0
    device_id = validate_device_id(read_value(args.device_id))
    passphrase = read_value(args.setup_password)
    subprocess.run(["/usr/sbin/nft", "-f", "/etc/millennium/wifi-setup.nft"], check=True)
    try:
        NetworkManager().start_setup(device_id, passphrase)
    except Exception:
        subprocess.run(["/usr/sbin/nft", "delete", "table", "inet",
                        "millennium_wifi_setup"], check=False)
        try:
            requested.unlink()
        except FileNotFoundError:
            pass
        raise
    Path("/run/millennium-wifi").mkdir(mode=0o750, parents=True, exist_ok=True)
    Path("/run/millennium-wifi/setup-active").touch(mode=0o640)
    status = state / "status.json"
    status.write_text(json.dumps({"state": "setup"}), encoding="utf-8")
    os.chmod(status, 0o644)
    try:
        requested.unlink()
    except FileNotFoundError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
