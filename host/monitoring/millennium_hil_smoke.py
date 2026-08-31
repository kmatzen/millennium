#!/usr/bin/env python3
"""Nightly, non-destructive smoke test against the installed physical phone."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request


def get_json(url, timeout=5):
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return json.loads(response.read().decode())


def service_active(name):
    return subprocess.run(
        ["systemctl", "is-active", "--quiet", name],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def metric_value(payload, name):
    for group in ("gauges", "counters"):
        if name in payload.get(group, {}):
            return payload[group][name]
    return None


def evaluate(health, version, metrics, release, devices, services, ota):
    checks = {
        "daemon_health": health.get("overall_status") in ("HEALTHY", "WARNING"),
        "host_version": version.get("version") == release.get("version"),
        "mcu_protocol": metric_value(metrics, "mcu_protocol_version") == 2,
        "keypad_present": bool(devices.get("keypad")),
        "display_present": bool(devices.get("display")),
        "daemon_service": bool(services.get("daemon.service")),
        "maintenance_tunnel": bool(services.get("millennium-maintenance-tunnel.service")),
        "ota_not_failed": ota.get("state") not in
            ("error", "rolled-back", "quarantined"),
    }
    return checks


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".hil-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w") as stream:
            json.dump(value, stream, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8081")
    parser.add_argument("--current", type=Path, default=Path("/opt/millennium/current"))
    parser.add_argument("--ota-status", type=Path,
                        default=Path("/var/lib/millennium/ota/status.json"))
    parser.add_argument("--keypad", type=Path, default=Path(
        "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00"))
    parser.add_argument("--display", type=Path, default=Path(
        "/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00"))
    parser.add_argument("--output", type=Path,
                        default=Path("/var/lib/millennium/hil/last-result.json"))
    args = parser.parse_args()

    try:
        health = get_json(args.base_url + "/api/health")
        version = get_json(args.base_url + "/api/version")
        metrics = get_json(args.base_url + "/api/metrics")
        release = json.loads((args.current / "release.json").read_text())
        ota = json.loads(args.ota_status.read_text())
        checks = evaluate(
            health, version, metrics, release,
            {"keypad": args.keypad.exists(), "display": args.display.exists()},
            {name: service_active(name) for name in
             ("daemon.service", "millennium-maintenance-tunnel.service")}, ota)
        error = None
    except Exception as exc:
        checks = {"collection": False}
        error = str(exc)
    report = {
        "schema": 1,
        "checked_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "passed": all(checks.values()),
        "checks": checks,
    }
    if error:
        report["error"] = error
    atomic_json(args.output, report)
    print(json.dumps(report, sort_keys=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
