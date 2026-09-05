#!/usr/bin/env python3
"""Durably arm and reconcile physical phone interruption tests."""

import argparse
from datetime import datetime, timezone
import fcntl
import json
import os
from pathlib import Path
import subprocess
import tempfile


SCENARIOS = {
    "cold_boot": (True, "Shut down, remove power, wait 30 seconds, then restore power."),
    "ringer_audio_peak": (False, "Place the measured ringer/audio load, then reconcile."),
    "coin_validator": (False, "Exercise the measured coin-validator load, then reconcile."),
    "controlled_brownout": (True, "Apply the documented controlled brownout, then restore nominal power."),
    "idle_power_loss": (True, "Remove power while the phone is idle, then restore it."),
    "active_call_power_loss": (True, "Remove power during an active call, then restore it."),
    "content_save_power_loss": (True, "Remove power during the documented content-state save, then restore it."),
    "ota_download_interruption": (False, "Interrupt and restore the network during OTA download, then reconcile."),
    "mcu_flash_interruption": (True, "Remove power during the selected MCU flash, then restore it."),
    "host_activation_interruption": (True, "Remove power after activation starts and before commit, then restore it."),
}


def now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".physical-test-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def unique_evidence_path(directory, scenario, stamp):
    directory.mkdir(parents=True, exist_ok=True)
    candidate = directory / ("%s-%s.json" % (scenario, stamp))
    sequence = 1
    while candidate.exists():
        candidate = directory / ("%s-%s-%d.json" % (scenario, stamp, sequence))
        sequence += 1
    return candidate


def read_text(path):
    try:
        return path.read_text(encoding="utf-8").strip()
    except OSError:
        return None


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def active(service):
    return subprocess.run(["systemctl", "is-active", "--quiet", service],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0


def resolved(path):
    try:
        return str(path.resolve(strict=True))
    except OSError:
        return None


def capture_snapshot(args):
    hil_output = args.state_dir / "last-hil.json"
    result = subprocess.run([str(args.hil_command), "--output", str(hil_output)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ota = read_json(args.ota_status) or {}
    return {
        "captured_at": now(),
        "boot_id": read_text(args.boot_id),
        "active_release": resolved(args.current_release),
        "active_content": resolved(args.current_content),
        "installed_sequence": read_text(args.installed_sequence),
        "ota_state": ota.get("state"),
        "ota_sequence": ota.get("sequence"),
        "firmware_digests": {
            "keypad": read_text(args.firmware_dir / "keypad.sha256"),
            "display": read_text(args.firmware_dir / "display.sha256"),
        },
        "services": {
            "daemon": active("daemon.service"),
            "maintenance_tunnel": active("millennium-maintenance-tunnel.service"),
        },
        "hil": read_json(hil_output),
        "hil_exit_status": result.returncode,
    }


def snapshot_healthy(snapshot, scenario=None):
    rollback_scenarios = {"mcu_flash_interruption", "host_activation_interruption"}
    ota_state = snapshot.get("ota_state")
    ota_ok = ota_state in ("committed", "current", "idle", "available", "pending")
    if scenario in rollback_scenarios and ota_state == "rolled-back":
        ota_ok = True
    hil = snapshot.get("hil", {})
    hil_checks = hil.get("checks", {})
    hil_ok = snapshot.get("hil_exit_status") == 0 and hil.get("passed") is True
    if scenario in rollback_scenarios and ota_state == "rolled-back":
        hil_ok = bool(hil_checks) and all(
            passed for name, passed in hil_checks.items() if name != "ota_not_failed")
    return {
        "boot_id_present": bool(snapshot.get("boot_id")),
        "release_present": bool(snapshot.get("active_release")),
        "content_present": bool(snapshot.get("active_content")),
        "daemon_active": snapshot.get("services", {}).get("daemon") is True,
        "maintenance_tunnel_active": snapshot.get("services", {}).get("maintenance_tunnel") is True,
        "hil_passed_or_expected_rollback": hil_ok,
        "ota_not_failed": ota_ok,
    }


def common_state(args):
    args.state_dir.mkdir(parents=True, exist_ok=True)
    return args.state_dir / "armed.json"


def arm(args):
    armed = common_state(args)
    if armed.exists():
        raise SystemExit("a physical interruption test is already armed")
    requires_reboot, instruction = SCENARIOS[args.scenario]
    before = capture_snapshot(args)
    checks = snapshot_healthy(before)
    if not all(checks.values()):
        raise SystemExit("refusing to arm from an unhealthy baseline")
    record = {
        "schema": 1, "operation": "physical-interruption-test",
        "status": "armed", "scenario": args.scenario, "armed_at": now(),
        "requires_boot_change": requires_reboot,
        "measurement_required": args.scenario in
            ("cold_boot", "ringer_audio_peak", "coin_validator", "controlled_brownout"),
        "instruction": instruction, "before": before,
    }
    atomic_json(armed, record)
    print(json.dumps({"armed": args.scenario, "instruction": instruction}, sort_keys=True))


def reconcile(args):
    armed = common_state(args)
    if not armed.exists():
        print("no physical interruption test is armed")
        return 0
    record = read_json(armed)
    if not record or record.get("schema") != 1 or record.get("status") != "armed":
        raise SystemExit("invalid armed physical-test record")
    after = capture_snapshot(args)
    before = record.get("before", {})
    boot_changed = before.get("boot_id") != after.get("boot_id")
    if record.get("requires_boot_change") and not boot_changed:
        print("pending: the required boot change has not occurred")
        return 2
    scenario = record["scenario"]
    checks = snapshot_healthy(after, scenario)
    checks["required_boot_change"] = boot_changed if record.get("requires_boot_change") else True
    checks["release_target_valid"] = bool(after.get("active_release"))
    checks["content_target_valid"] = bool(after.get("active_content"))
    if after.get("ota_state") == "rolled-back":
        checks["prior_release_restored"] = (
            before.get("active_release") == after.get("active_release"))
        if scenario == "mcu_flash_interruption":
            checks["prior_firmware_restored"] = (
                before.get("firmware_digests") == after.get("firmware_digests"))
    recovered = all(checks.values())
    completed = dict(record)
    completed.update({
        "status": "system-recovered" if recovered else "unsafe",
        "completed_at": now(), "after": after, "checks": checks,
        "physical_observation_required": True,
        "automatic_acceptance_claimed": False,
    })
    stamp = completed["completed_at"].replace(":", "").replace("+00:00", "Z")
    output = unique_evidence_path(args.state_dir / "evidence", record["scenario"], stamp)
    atomic_json(output, completed)
    if recovered:
        armed.unlink()
        directory = os.open(args.state_dir, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        print(output)
        return 0
    print("unsafe recovery; armed state retained; evidence: %s" % output)
    return 1


def status(args):
    armed = common_state(args)
    if armed.exists():
        print(armed.read_text(encoding="utf-8").strip())
    else:
        print('{"status":"idle"}')


def add_paths(parser):
    parser.add_argument("--state-dir", type=Path,
                        default=Path("/var/lib/millennium/physical-tests"))
    parser.add_argument("--boot-id", type=Path,
                        default=Path("/proc/sys/kernel/random/boot_id"))
    parser.add_argument("--current-release", type=Path,
                        default=Path("/opt/millennium/current"))
    parser.add_argument("--current-content", type=Path,
                        default=Path("/var/lib/millennium/content/current"))
    parser.add_argument("--ota-status", type=Path,
                        default=Path("/var/lib/millennium/ota/status.json"))
    parser.add_argument("--installed-sequence", type=Path,
                        default=Path("/var/lib/millennium/ota/installed-sequence"))
    parser.add_argument("--firmware-dir", type=Path,
                        default=Path("/var/lib/millennium/firmware"))
    parser.add_argument("--hil-command", type=Path,
                        default=Path("/usr/local/libexec/millennium-hil-smoke"))


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    arm_parser = commands.add_parser("arm")
    arm_parser.add_argument("--scenario", choices=SCENARIOS, required=True)
    add_paths(arm_parser)
    arm_parser.set_defaults(function=arm)
    reconcile_parser = commands.add_parser("reconcile")
    add_paths(reconcile_parser)
    reconcile_parser.set_defaults(function=reconcile)
    status_parser = commands.add_parser("status")
    status_parser.add_argument("--state-dir", type=Path,
                               default=Path("/var/lib/millennium/physical-tests"))
    status_parser.set_defaults(function=status)
    args = parser.parse_args()
    args.state_dir.mkdir(parents=True, exist_ok=True)
    lock_descriptor = os.open(args.state_dir / ".lock", os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(lock_descriptor, fcntl.LOCK_EX)
        result = args.function(args)
    finally:
        os.close(lock_descriptor)
    raise SystemExit(result or 0)


if __name__ == "__main__":
    main()
