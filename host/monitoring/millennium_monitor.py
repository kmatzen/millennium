#!/usr/bin/env python3
"""Local appliance monitor with Prometheus textfile output and failing exit."""

import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import time
import urllib.request


def metric(name, value, labels=None):
    suffix = ""
    if labels:
        escaped = []
        for key, label_value in sorted(labels.items()):
            label_text = str(label_value).replace("\\", "\\\\").replace('"', '\\"')
            escaped.append(f'{key}="{label_text}"')
        suffix = "{" + ",".join(escaped) + "}"
    return f"millennium_{name}{suffix} {value}"


def get_json(url, timeout=5):
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return json.loads(response.read().decode())


def numeric_metric(payload, name):
    """Read a daemon counter or gauge without confusing missing with zero."""
    for group in ("counters", "gauges"):
        values = payload.get(group, {})
        if name in values:
            return float(values[name])
    return 0.0


def service_active(name):
    result = subprocess.run(["systemctl", "is-active", "--quiet", name],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return result.returncode == 0


def recent_filesystem_errors():
    result = subprocess.run(
        ["journalctl", "--quiet", "--kernel", "--priority=err",
         "--since=-15 minutes", "--output=cat"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        timeout=10, check=False)
    if result.returncode != 0:
        return -1
    markers = ("I/O error", "EXT4-fs error", "Buffer I/O", "read-only file system")
    return sum(any(marker.lower() in line.lower() for marker in markers)
               for line in result.stdout.splitlines())


def certificate_days(host):
    context = ssl.create_default_context()
    with socket.create_connection((host, 443), timeout=5) as raw:
        with context.wrap_socket(raw, server_hostname=host) as tls:
            expires = ssl.cert_time_to_seconds(tls.getpeercert()["notAfter"])
    return max(0, int((expires - time.time()) / 86400))


def atomic_write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".monitor-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8081")
    parser.add_argument("--output", type=Path,
                        default=Path("/var/lib/node_exporter/textfile_collector/millennium.prom"))
    parser.add_argument("--state-dir", type=Path, default=Path("/var/lib/millennium/ota"))
    parser.add_argument("--backup-stamp", type=Path,
                        default=Path("/var/lib/millennium/backup/last-success"))
    parser.add_argument("--hil-result", type=Path,
                        default=Path("/var/lib/millennium/hil/last-result.json"))
    parser.add_argument("--updates-host", default="updates.kmatzen.com")
    parser.add_argument("--daemon-service", default="daemon.service")
    parser.add_argument("--tunnel-service",
                        default="millennium-maintenance-tunnel.service")
    args = parser.parse_args()

    lines = []
    failed = False
    now = int(time.time())
    try:
        health = get_json(args.base_url + "/api/health")
        serving = health.get("overall_status") in ("HEALTHY", "WARNING")
        lines.append(metric("daemon_serving", int(serving)))
        failed |= not serving
    except Exception:
        lines.append(metric("daemon_serving", 0))
        failed = True

    try:
        state = get_json(args.base_url + "/api/state")
        lines.append(metric("sip_registered", int(bool(state.get("sip_registered")))))
    except Exception:
        lines.append(metric("sip_registered", 0))
        failed = True

    try:
        daemon_metrics = get_json(args.base_url + "/api/metrics")
        for source_name in ("serial_disconnects", "serial_reconnects",
                            "mcu_resets_keypad", "mcu_resets_display",
                            "arduino_i2c_drops_keypad",
                            "arduino_i2c_drops_display"):
            lines.append(metric(source_name,
                                numeric_metric(daemon_metrics, source_name)))
    except Exception:
        lines.append(metric("metrics_collection_error", 1))
        failed = True

    for service in (args.daemon_service, args.tunnel_service):
        active = service_active(service)
        lines.append(metric("service_active", int(active), {"service": service}))
        failed |= not active

    status_path = args.state_dir / "status.json"
    try:
        ota = json.loads(status_path.read_text())
        state_name = ota.get("state", "unknown")
        ota_error = state_name in ("error", "rolled-back")
        lines.append(metric("ota_error", int(ota_error)))
        lines.append(metric("ota_status_timestamp_seconds", int(ota.get("updated_at", 0))))
        failed |= ota_error
    except Exception:
        lines.append(metric("ota_error", 1))
        failed = True

    disk = shutil.disk_usage("/")
    lines.append(metric("root_filesystem_free_bytes", disk.free))
    failed |= disk.free < 512 * 1024 * 1024

    fs_errors = recent_filesystem_errors()
    lines.append(metric("filesystem_errors_recent", fs_errors))
    failed |= fs_errors != 0

    try:
        backup_age = max(0, now - int(args.backup_stamp.stat().st_mtime))
        lines.append(metric("backup_age_seconds", backup_age))
        failed |= backup_age > 36 * 60 * 60
    except OSError:
        lines.append(metric("backup_age_seconds", -1))
        failed = True

    try:
        hil = json.loads(args.hil_result.read_text())
        hil_age = max(0, now - int(args.hil_result.stat().st_mtime))
        hil_passed = bool(hil.get("passed"))
        lines.append(metric("hil_smoke_passed", int(hil_passed)))
        lines.append(metric("hil_smoke_age_seconds", hil_age))
        failed |= not hil_passed or hil_age > 36 * 60 * 60
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        lines.append(metric("hil_smoke_passed", 0))
        lines.append(metric("hil_smoke_age_seconds", -1))
        failed = True

    try:
        lines.append(metric("update_certificate_days_remaining",
                            certificate_days(args.updates_host)))
    except Exception:
        lines.append(metric("update_certificate_days_remaining", -1))
        failed = True

    try:
        boot_age = float(Path("/proc/uptime").read_text().split()[0])
        lines.append(metric("boot_timestamp_seconds", int(now - boot_age)))
    except Exception:
        lines.append(metric("boot_timestamp_seconds", 0))

    lines.append(metric("last_checkin_timestamp_seconds", now))
    atomic_write(args.output, "\n".join(lines) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
